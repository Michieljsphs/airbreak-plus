#!/bin/bash
# Print the humidifier and climate variables of a running AirSense 10 (SX567
# 0401 firmware) over SWD. Reads RAM through OpenOCD without halting, so
# therapy keeps running. Never halt or reset the target: that stops the blower.
#
# Usage: ./read-climate.sh [-w SECONDS] [run-ocd.sh options]
#   -w SECONDS   repeat every SECONDS until interrupted
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
passthrough=()
while (( $# > 0 )); do
    case "$1" in
        -w)
            (( $# >= 2 )) || { echo "Usage: $0 [-w SECONDS] [run-ocd.sh options]" >&2; exit 2; }
            interval=$2
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
    awk -v vars="$VARIABLES" '
        function hex(s,   i, n, c) {
            n = 0
            s = tolower(s)
            for (i = 1; i <= length(s); i++) {
                c = index("0123456789abcdef", substr(s, i, 1)) - 1
                n = n * 16 + c
            }
            return n
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
                printf "%-4s %-10d %10.2f  %-5s %s\n", name[key], raw, raw / scale[key], unit[key], text[key]
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
