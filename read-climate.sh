#!/bin/bash
# Print the humidifier and climate variables of a running AirSense 10 (SX567
# 0401 firmware) over SWD. Reads RAM through OpenOCD without halting, so
# therapy keeps running. Never halt or reset the target: that stops the blower.
#
# Usage: ./read-climate.sh [-w SECONDS] [-r ROOM_DEGC] [-f FACTOR] [-c COUPLING]
#                          [-k K1] [run-ocd.sh options]
#   -w SECONDS   repeat every SECONDS until interrupted
#   -r ROOM_DEGC also print the ambient RH the same moisture gives at that room
#                temperature; the sensor sits in the warm case and reads low
#   -f FACTOR    Warm-up Factor used for the mask-end estimate (default 1.00)
#   -c COUPLING  Warm-up Coupling used for it (default 0.30)
#   -k K1        humidifier calibration k1 (default 0.001; k0 is taken as 0)
# Set -f and -c to the values in the device menu. The estimate is the one the
# header uses: room air plus the smaller of the plate and heater-power bounds.
# Other options, such as -s SERIAL, are passed to run-ocd.sh.

# name  address  scale  unit  description
VARIABLES='
AFL 0x2000987C 100 L/min average blower flow
HPT 0x200098BC 10 degC humidifier plate temperature
HTT 0x200098CC 10 degC heated tube temperature, measured
HTS 0x2000986C 10 degC heated tube temperature setpoint
HPM 0x200098E4 1 % humidifier heater duty (PWM median)
HAT 0x20009DDC 10 degC ambient temperature
HAH 0x20009DD4 10 % ambient relative humidity
'

interval=
room=
factor=1.00
coupling=0.30
k1=0.001
passthrough=()
usage="Usage: $0 [-w SECONDS] [-r ROOM_DEGC] [-f FACTOR] [-c COUPLING] [-k K1] [run-ocd.sh options]"
while (( $# > 0 )); do
    case "$1" in
        -w|-r|-f|-c|-k)
            (( $# >= 2 )) || { echo "$usage" >&2; exit 2; }
            case "$1" in
                -w) interval=$2 ;;
                -r) room=$2 ;;
                -f) factor=$2 ;;
                -c) coupling=$2 ;;
                -k) k1=$2 ;;
            esac
            shift 2
            ;;
        *)
            passthrough+=("$1")
            shift
            ;;
    esac
done

cd "$(dirname "$0")" || exit

read_once() {
    local commands=() name address rest output
    while read -r name address rest; do
        [[ -n "$name" ]] && commands+=(-c "mdw $address")
    done <<< "$VARIABLES"
    output=$(./run-ocd.sh "${passthrough[@]}" "${commands[@]}" -c shutdown 2>&1) || {
        echo "$output" >&2
        return 1
    }
    # mdw prints "0x2000987c: 00000c41"; values are signed 32-bit words.
    awk -v vars="$VARIABLES" -v room="$room" -v factor="$factor" \
        -v coupling="$coupling" -v k1="$k1" '
        function hex(s,   i, n, c) {
            n = 0
            s = tolower(s)
            for (i = 1; i <= length(s); i++) {
                c = index("0123456789abcdef", substr(s, i, 1)) - 1
                n = n * 16 + c
            }
            return n
        }
        function ahsat(t) { return 4.61 + t * (0.436 + t * (0.00122 + t * 0.000427)) }
        function dewpoint(ah,   lo, hi, mid, i) {
            lo = -20; hi = 70
            for (i = 0; i < 40; i++) {
                mid = (lo + hi) / 2
                if (ahsat(mid) < ah) lo = mid; else hi = mid
            }
            return (lo + hi) / 2
        }
        BEGIN {
            count = split(vars, lines, "\n")
            for (i = 1; i <= count; i++) {
                n = split(lines[i], f, " ")
                if (n < 5) continue
                key = tolower(f[2])
                name[key] = f[1]; scale[key] = f[3]; unit[key] = f[4]
                desc = ""
                for (j = 5; j <= n; j++) desc = desc (j > 5 ? " " : "") f[j]
                text[key] = desc
                order[++total] = key
            }
        }
        /^0x[0-9a-fA-F]+: [0-9a-fA-F]+/ {
            key = tolower(substr($1, 1, length($1) - 1))
            value[key] = hex($2)
        }
        END {
            printf "%-4s %-10s %10s  %-5s %s\n", "Var", "Raw", "Value", "Unit", "Meaning"
            for (i = 1; i <= total; i++) {
                key = order[i]
                if (!(key in value)) {
                    printf "%-4s %-10s\n", name[key], "(no data)"
                    continue
                }
                raw = value[key]
                if (raw >= 2147483648) raw -= 4294967296
                byname[name[key]] = raw
                printf "%-4s %-10d %10.2f  %-5s %s\n", name[key], raw, raw / scale[key], unit[key], text[key]
            }
            if (("HAT" in byname) && ("HAH" in byname)) {
                amb = byname["HAT"] / 10
                rh = byname["HAH"] / 1000
                room_ah = rh * ahsat(amb)
                printf "\n%-22s %8s  %-5s %s\n", "Ambient", "Value", "Unit", "Meaning"
                printf "%-22s %8.1f  %-5s %s\n", "Moisture", room_ah, "g/m3", \
                    sprintf("sensor air: %.1f degC, %.1f %% RH", amb, rh * 100)
                if (room != "")
                    printf "%-22s %8.1f  %-5s %s\n", "RH at room temp", 100 * room_ah / ahsat(room), "%", \
                        sprintf("same moisture at %.1f degC", room)
            }
            if (("HAT" in byname) && ("HAH" in byname) && ("HPT" in byname) && ("HTT" in byname) && ("AFL" in byname) && ("HPM" in byname)) {
                plate = byname["HPT"] / 10
                tube = byname["HTT"] / 10
                flow = byname["AFL"] / 100
                duty = byname["HPM"] / 100
                plate_bound = ahsat(amb + coupling * (plate - amb)) - ahsat(amb)
                added = plate_bound
                power = -1
                if (flow >= 8 && k1 > 0) {
                    power = duty / (k1 * flow)
                    if (power < added) added = power
                }
                if (added < 0) added = 0
                mask = room_ah + factor * added
                printf "\n%-22s %8s  %-5s %s\n", "Mask-end estimate", "Value", "Unit", \
                    sprintf("Meaning (factor %.2f, coupling %.2f, k1 %g)", factor, coupling, k1)
                printf "%-22s %8.1f  %-5s %s\n", "Plate bound", plate_bound, "g/m3", "added moisture from the plate temperature"
                if (power >= 0)
                    printf "%-22s %8.1f  %-5s %s\n", "Heater-power bound", power, "g/m3", "added moisture from the heater duty and flow"
                else
                    printf "%-22s %8s  %-5s %s\n", "Heater-power bound", "-", "g/m3", "not used below 8 L/min"
                printf "%-22s %8.1f  %-5s %s\n", "Added moisture", factor * added, "g/m3", "smaller bound x factor"
                printf "%-22s %8.1f  %-5s %s\n", "Absolute humidity", mask, "g/m3", "room moisture + added moisture"
                printf "%-22s %8.0f  %-5s %s\n", "RH at the tube", 100 * mask / ahsat(tube), "%", \
                    sprintf("at the measured %.1f degC (not limited to 100)", tube)
                printf "%-22s %8.1f  %-5s %s\n", "Dew point", dewpoint(mask), "degC", "of the mask-end air"
            }
        }
    ' <<< "$output"
}

if [[ -z "$interval" ]]; then
    read_once
else
    while true; do
        date +%T
        read_once
        echo
        sleep "$interval"
    done
fi
