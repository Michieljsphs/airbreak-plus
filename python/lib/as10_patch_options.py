"""Air10 patch options."""

import argparse

from .patch_options import Parameter, Patch, Phase, Platform, Value


def percentage(value):
    try:
        percent = float(value)
    except ValueError:
        percent = None
    if percent is None or not 0 <= percent <= 100:
        raise argparse.ArgumentTypeError('Percentage from 0 to 100 expected.')
    return percent


def graph_width(value):
    """Chart width as hours:minutes, from 0:10 to 12:00; returned in minutes."""
    try:
        hours, separator, minutes = value.strip().partition(':')
        total = int(hours) * 60 + int(minutes)
        valid = separator and 0 <= int(minutes) < 60 and 10 <= total <= 12 * 60
    except ValueError:
        valid = False
    if not valid:
        raise argparse.ArgumentTypeError('Width as hours:minutes from 0:10 to 12:00, such as 8:30, expected.')
    return total


def graph_width_text(minutes):
    return '%d:%02d' % divmod(minutes, 60)


def tube_temperature(value):
    try:
        degrees = int(value)
    except ValueError:
        degrees = None
    if degrees is None or not 16 <= degrees <= 30:
        raise argparse.ArgumentTypeError('Whole degrees C from 16 to 30 expected.')
    return degrees

def cmh2o_start_ps(value):
    """Start PS in cmH2O, or -1 for off."""
    if str(value).lower() == 'off':
        return -1.0
    try:
        cmh2o = float(value)
    except ValueError:
        cmh2o = None
    if cmh2o is None or not 0 <= cmh2o <= 10 or abs(cmh2o * 5 - round(cmh2o * 5)) > 1e-6:
        raise argparse.ArgumentTypeError('off, or 0 to 10 cmH2O in steps of 0.2, expected.')
    return cmh2o


def start_ps_text(value):
    return 'off' if value < 0 else str(value)

# Air10 GUI palette roles (SX567): groups of palette entries that share one
# stock color, named after how the stock colors are used (white text on
# black, ResMed blue accent).
# (role, label, --patch-custom-palette color or None, stock color, entry
# offsets in the palette table). Roles without a custom color are left as
# they are unless --palette-colors names them.
PALETTE_ROLES = (
    ('text', 'Text', 'FFBB44', 'FFFFFF', (0x00, 0x1C, 0x28, 0x40, 0x44, 0x48)),
    ('secondary', 'Secondary text', '964848', '969696', (0x04, 0x0C)),
    ('dim', 'Dimmed text', '643232', '646464', (0x08,)),
    ('frame', 'Frames and bars', '402020', '404040', (0x10, 0x14, 0x30, 0x4C)),
    ('background', 'Background', '080008', '000000', (0x18, 0x34, 0x50)),
    ('accent', 'Accent (selection)', 'CC3300', '0099CC', (0x24,)),
    ('accent_dark', 'Dark accent', None, '003953', (0x2C, 0x38)),
    ('grey', 'Grey', None, '969696', (0x20,)),
    ('light', 'Light grey', None, 'D0D0D0', (0x3C,)),
)
PALETTE_KEYS = tuple(role[0] for role in PALETTE_ROLES)


def palette_colors(value):
    """ROLE=RRGGBB[,ROLE=RRGGBB...] as a dict in role order."""
    colors = {}
    for item in filter(None, (part.strip() for part in value.split(','))):
        key, sep, color = item.partition('=')
        key, color = key.strip().lower(), color.strip().lstrip('#').upper()
        if not sep or key not in PALETTE_KEYS:
            raise argparse.ArgumentTypeError(
                'expected ROLE=RRGGBB with ROLE one of: %s' % ', '.join(PALETTE_KEYS))
        if len(color) != 6 or any(c not in '0123456789ABCDEF' for c in color):
            raise argparse.ArgumentTypeError('%s: expected a hex color like FFBB44' % key)
        colors[key] = color
    return {key: colors[key] for key in PALETTE_KEYS if key in colors}


def format_palette_colors(colors):
    return ','.join('%s=%s' % item for item in colors.items())


# Patch order is part of the implementation. Payloads must be present before
# custom settings patch their ABI slots; all MOP handlers must be registered
# before finalization installs the shared dispatcher and expands the variable
# callback table; CRCs are always last.
PLATFORM = Platform('air10', categories=(
    'Therapy unlocks',
    'Therapy data and reporting',
    'Quality of life',
    'Therapy modifications',
    'Miscellaneous',
), phases=(
    Phase('Firmware preparation', (
        Patch('patch-font-repack', 'Reclaim flash space by compacting font bitmaps',
              True, 'patch_font_repack', category='Miscellaneous'),
        Patch('patch-expand-heap', 'Give patches about 13.5 KiB more working memory (SX567)',
              True, 'patch_expand_heap', category='Miscellaneous'),
    )),

    Phase('Therapy unlocks', (
        Patch('patch-extra-modes', 'Unlock all therapy modes',
              True, 'extra_modes', category='Therapy unlocks'),
        Patch('patch-gui-config', 'Enable the clinical settings of the unlocked modes',
              True, 'gui_config', category='Therapy unlocks'),
        Patch('patch-unlock-options', 'Unlock hidden setting choices',
              True, 'unlock_option_masks', category='Therapy unlocks'),
        Patch('patch-unlock-uilimits', 'Allow pressure settings from 1 to 30 cmH2O',
              True, 'unlock_ui_limits', category='Therapy unlocks'),
        Patch('patch-asv-ps-range', 'Unlock ASV/ASVAuto PS constraints',
              True, 'asv_unlock_ps_range', category='Therapy unlocks'),
        Patch('patch-adjustable-mcp', 'Allow lowering the ASV pressure cap',
              False, 'patch_adjustable_mcp', category='Therapy unlocks'),
        Patch('patch-ivaps-settings', 'Restore iVAPS clinical settings',
              True, 'enable_ivaps_settings', category='Therapy unlocks',
              requires=('patch-custom-settings',)),
        Patch('patch-ivaps-ps-range', 'Unlock iVAPS PS constraints',
              True, 'ivaps_unlock_ps_range', category='Therapy unlocks'),
    )),

    Phase('Quality of life', (
        Patch('patch-defaults', 'Change factory defaults',
              True, 'patch_defaults', category='Quality of life'),
        Patch('patch-unlock-languages', 'Unlock all built-in languages',
              True, 'unlock_languages', category='Quality of life'),
        Patch('patch-language-fonts', 'Use the right font for each language',
              True, 'patch_language_fonts', category='Quality of life'),
        Patch('patch-font-ua', 'Add Ukrainian glyphs to the standard GUI font',
              False, 'patch_font_ua', category='Quality of life'),
        Patch('patch-therapy-screen',
              'Add a choice of therapy screen styles, with optional flow and pressure graph',
              True, 'patch_therapy_screen', category='Quality of life'),
        Patch('patch-graph-climate',
              'Show the climate chart instead of the flow and pressure trace: plate and tube temperatures '
              'and estimated RH over --graph-width.',
              False, 'patch_graph_climate', category='Quality of life',
              requires=('patch-therapy-screen',), parameters=(
                  Parameter('graph-width', 'Time the climate chart covers, 0:10 to 12:00. Default: 1:00.',
                            Value(graph_width, 'H:MM', format=graph_width_text), 60),
              )),
        Patch('patch-graph-keep-screen-on',
              'Keep the screen on during therapy while the graph is shown',
              False, 'patch_graph_keep_screen_on', category='Quality of life',
              requires=('patch-therapy-screen',)),
        Patch('patch-header-clock', 'Show the time in the Home and therapy screen titles',
              True, 'patch_header_clock', category='Quality of life'),
        Patch('patch-header-climate',
              'Show tube and plate temperatures and estimated RH in the Home and therapy-screen headers; hides the page counter there.',
              False, 'patch_header_climate', category='Quality of life', requires=('patch-header-clock',)),
        Patch('patch-header-climate-decimal',
              'Show the climate readout temperatures with one decimal (27.4 instead of 27).',
              False, 'patch_header_climate_decimal', category='Quality of life', requires=('patch-header-climate',)),
        Patch('patch-header-climate-always',
              'Show the climate readout in every screen title (menus too), not only Home and therapy.',
              False, 'patch_header_climate_always', category='Quality of life', requires=('patch-header-climate',)),
        Patch('patch-fw-backlight', 'Improve backlight adaptation to ambient light',
              True, 'patch_backlight_adapt', category='Quality of life'),
        Patch('patch-keep-backlight', 'Toggle screen keep-awake by holding the encoder',
              True, 'enable_keep_backlight', category='Quality of life'),
        Patch('patch-humidifier-hold', 'Switch the humidifier off and on by holding Home for 2 s.',
              False, 'patch_humidifier_hold', category='Quality of life'),
        Patch('patch-custom-palette', 'Patch the custom color palette',
              True, 'custom_palette', category='Quality of life', parameters=(
                  Parameter('palette-colors',
                            'Single palette colors, given once; roles left out keep the custom palette. '
                            'Roles: %s.' % ', '.join(PALETTE_KEYS),
                            Value(palette_colors, 'ROLE=RRGGBB,...',
                                  format=format_palette_colors)),
              )),
        Patch('patch-target-rh',
              'Set the Climate Control target humidity; custom-settings adds menu control',
              True, 'patch_target_rh', category='Quality of life', parameters=(
                  Parameter('target-rh', 'Target RH, 0..100%. Default: 85.',
                            Value(percentage, 'PERCENT'), 85.0),
              )),
        Patch('patch-humid-start',
              'Hold the heated tube back while the humidifier warms up',
              False, 'patch_humid_start', category='Quality of life',
              requires=('patch-font-repack',), parameters=(
                  Parameter('humid-start-min-tube',
                            'Humid Start: lowest tube temperature the warm-up cap goes to, whole '
                            'degC, 16..30. 16 is the lowest tube setting, so no extra minimum. '
                            'Default: 16.',
                            Value(tube_temperature, 'DEGC'), 16),
              )),
        Patch('patch-past-date', 'Allow setting past date in the menu and over UART',
              True, 'patch_past_date', category='Quality of life'),
        Patch('patch-motor-nagscreen', 'Remove the "Motor life exceeded" nag screen',
              True, 'motor_nagscreen', category='Quality of life'),
        Patch('patch-sd-warning',
              'Show the "No SD card detected" message when therapy starts without a card',
              True, 'patch_sd_warning', category='Quality of life'),
    )),

    Phase('Therapy modifications', (
        Patch('patch-fw-squarewave', 'Add Square Wave pressure in S, ST, T and PAC',
              False, 'patch_squarewave', category='Therapy modifications'),
        Patch('patch-fall-time', 'Add Fall Time setting in S, ST, T, PAC, iVAPS',
              False, 'patch_fall_time', category='Therapy modifications'),
        Patch('patch-st-ibr', 'Add adaptive backup-rate control in ST',
              False, 'patch_st_ibr', category='Therapy modifications'),
        Patch('patch-learn-targets', 'Add iVAPS Learn Targets on SX567',
              True, 'enable_learn_targets', category='Therapy modifications',
              requires=('patch-custom-settings',)),
        Patch('patch-autoepap', 'Add AutoEPAP control in iVAPS',
              False, 'patch_autoepap', category='Therapy modifications',
              requires=('patch-font-repack', 'patch-expand-heap')),
        Patch('patch-fw-asv-wrapper', 'Add backup-rate switch in ASV, ASVAuto',
              False, 'patch_asv_task_wrapper', category='Therapy modifications'),
        Patch('patch-fw-vauto-wrapper', 'Add Custom VAuto: adjustable pressure support and trigger/cycle assist in VAuto',
              False, 'patch_wrapper_limit_max_pdiff', category='Therapy modifications',
              parameters=(
                  Parameter('vauto-start-ps',
                            'Start PS (cmH2O): PS at the start of the ramp, rising to the set PS '
                            'by the end of the ramp. off, or 0..10 in steps of 0.2 (0 ramps from zero '
                            'support). Also the menu default; the menu row needs a free '
                            'reclaimed variable. Default: off.',
                            Value(cmh2o_start_ps, 'CMH2O', format=start_ps_text), -1.0,
                            off=-1.0, on_default=3.0),
              )),
    )),

    Phase('Custom settings integration', (
        Patch('patch-custom-settings', 'Add menu settings for the patches that need them',
              True, 'custom_patch_settings', category='Quality of life'),
    )),

    Phase('Therapy data and reporting', (
        Patch('patch-edf-merge', 'Record all available signals to the SD card in every therapy mode',
              True, 'patch_edf_merge', category='Therapy data and reporting'),
        Patch('patch-fw-vidspoof',
              'Report a device variant that matches the therapy mode, so myAir accepts the data',
              True, 'patch_vid_spoof', category='Therapy data and reporting'),
    )),

    Phase('Miscellaneous', (
        Patch('patch-integrity-check', 'Bypass firmware integrity checks',
              True, 'patch_integrity_check', category='Miscellaneous'),
        Patch('patch-bypass-psuid', 'Bypass Power Supply check at start-up',
              True, 'bypass_psucheck', category='Miscellaneous'),
        Patch('patch-uart-stream-schema',
              'Report live-stream layouts and custom-settings metadata over UART',
              True, 'patch_uart_stream_schema', category='Miscellaneous'),
        Patch('patch-blx-dump', 'Add firmware dump and EEPROM dump and restore over UART to the bootloader',
              True, 'patch_blx_dump', category='Miscellaneous'),
        Patch('patch-fw-lcd', 'Support ILI9325 and ILI9328 replacement displays (detected at startup)',
              False, 'patch_lcd_ili9325', category='Miscellaneous'),
    )),
))
