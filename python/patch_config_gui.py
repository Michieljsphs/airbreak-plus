"""Window for the Air 10 patch options in patch-airsense.config: tick patches, set parameters, save, patch.

The patches, their groups, defaults and dependencies are read from patch-airsense.py itself, so new patches
show up here without changes. Ticking a patch also ticks the patches it requires; unticking one unticks the
patches that require it. Options that differ from the built-in default are shown bold, and only those are
written by Save (see docs/patch_options.md). Other patcher options found in the file (--force-deprecated,
...) are kept as they are.
Hovering a patch shows its entry from docs/guide/patching.md and the linked feature page, its dependencies
and the custom settings menu items it adds. The counter above the patches shows how many of the reclaimed
Reminder variables (the menu slots) and labels the selected patches use; both are read from the patcher.
Patch saves the config and runs patch-airsense.py INPUT OUTPUT PATCH --overwrite, with its output in the
log. Build payloads runs make binaries (through WSL on Windows) to compile the payloads the patcher loads
from build/. Flash device does over SWD what docs/guide/flashing.md does in the OpenOCD telnet console: each
button runs OpenOCD once with tcl/airsense.cfg and one of its commands (dump, flash_new, ...), either a
Windows openocd.exe or run-ocd.sh in WSL. The paths and choices are remembered in ~/.airbreak_patch_gui.json.
Needs only Python with tkinter (included in the Windows installer). Start it with patch-airsense-gui.bat
(no console) or python python/patch_config_gui.py.
"""
import argparse
from copy import copy
import glob
import importlib.util
import inspect
import json
import os
import queue
import re
import shlex
import shutil
import subprocess
import sys
import textwrap
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))   # <repo>/python
REPO = os.path.dirname(HERE)
PATCHER = os.path.join(HERE, "patch-airsense.py")
sys.path.insert(0, HERE)   # patch-airsense.py imports lib.*
from lib.patch_config import AIR10_LEGACY_OPTIONS, LOCAL_FILES  # noqa: E402

TITLE = "AirSense 10 patch options"
CONFIG_FILE = os.path.join(REPO, LOCAL_FILES["air10"])
SETTINGS_FILE = os.path.join(os.path.expanduser("~"), ".airbreak_patch_gui.json")
HEADER = "# Written by python/patch_config_gui.py: only options that differ from the built-in defaults."
DEFAULT_OUTPUT = os.path.join(REPO, "build", "stm32-patched.bin")
DEFAULT_BACKUP = os.path.join(REPO, "stm32.bin")   # where docs/guide/openocd.md has the stock dump
FLASH_SIZE = 0x100000   # flash_new and dump work on the whole 1 MB flash
OPENOCD_GLOBS = (   # Windows installs of OpenOCD, newest version first within each
    r"C:\Program Files*\OpenOCD*\bin\openocd.exe",
    r"C:\Program Files*\xpack-openocd*\bin\openocd.exe",
    os.path.expanduser(r"~\AppData\Roaming\xPacks\@xpack-dev-tools\openocd\*\.content\bin\openocd.exe"),
    os.path.expanduser(r"~\ModusToolbox\tools_*\openocd\bin\openocd.exe"),
    r"C:\VSCode_OnethinxPack_Windows\tools_*\openocd\bin\openocd.exe",
    r"C:\Program Files*\Cypress\*\openocd\bin\openocd.exe",
    r"C:\Program Files*\Infineon\*\openocd\bin\openocd.exe",
)
GUIDE_DIR = os.path.join(REPO, "docs", "guide")
PATCHING_DOC = os.path.join(GUIDE_DIR, "patching.md")
P = None   # patch-airsense.py, loaded by main()
SLOTS = (("g8", "On/off settings"), ("g4", "Value settings"), ("labels", "New labels"))


def load_patcher():
    """patch-airsense.py has a hyphen in its name, so it is loaded from its path."""
    spec = importlib.util.spec_from_file_location("patch_airsense", PATCHER)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module   # inspect.getsource looks the module up by name
    spec.loader.exec_module(module)
    return module


def plain(text):
    """Markdown inline markup to plain text: links, code, emphasis and escaped pipes."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    return re.sub(r"[`*]", "", text).replace("\\|", "|").strip()


def doc_section(path, anchor):
    """First prose paragraph below the heading with this anchor, or below the title without one."""
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError:
        return ""
    start = 0
    for i, line in enumerate(lines):
        if line.startswith("#") and (not anchor or re.sub(r"[^a-z0-9 -]", "", line.lstrip("#").strip().lower())
                                     .replace(" ", "-") == anchor):
            start = i + 1
            break
    else:
        if anchor:
            return ""
    paragraph = []
    for line in lines[start:]:
        if line.startswith("#"):
            break
        if not line.strip() or line.lstrip().startswith(("<", "!", "|", "-", "```")):
            if paragraph:
                break
            continue
        paragraph.append(line.strip())
    return plain(" ".join(paragraph))


def patch_docs():
    """option -> (what it does, linked feature page or None, first paragraph of that page) from patching.md.
    The Therapy modifications table lists environment variables; they map to the last option they set."""
    try:
        with open(PATCHING_DOC, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return {}
    rows = [line for line in text.splitlines() if line.startswith("|")]
    docs = {}
    for row in rows:
        cells = [c.strip() for c in re.split(r"(?<!\\)\|", row.strip().strip("|"))]
        if len(cells) < 2:
            continue
        options = re.findall(r"--(patch-[a-z0-9-]+)", cells[-1])
        env = re.match(r"`(\w+)=1`", cells[0])
        if env and env.group(1) in AIR10_LEGACY_OPTIONS:
            options = [o[2:] for o in AIR10_LEGACY_OPTIONS[env.group(1)] if o.startswith("--patch-")][-1:]
        what = cells[1] if len(cells) > 2 else cells[-1]
        link = re.search(r"\]\(([^)#]+)(?:#([^)]+))?\)", row)
        page = os.path.normpath(os.path.join(GUIDE_DIR, link.group(1))) if link else None
        for option in options:
            docs.setdefault(option, (plain(what), page, doc_section(page, link.group(2)) if link else ""))
    for paragraph in re.split(r"\n\s*\n", text):   # options described only in prose, like keep-screen-on
        if paragraph.lstrip().startswith(("|", "#", "```", "<", "-")):
            continue
        for option in re.findall(r"--(patch-[a-z0-9-]+)", paragraph):
            docs.setdefault(option, (plain(" ".join(paragraph.split())), None, ""))
    return docs


def menu_resources():
    """Which custom settings menu resources each patch uses, read from the patcher source:
    {option: {"g8": [vars], "g4": [vars], "labels": [texts], "free_g8": (candidates, labels) or None}} and the
    pool size of each resource. free_g8 is a setting that takes the first unclaimed on/off variable of the
    candidates, if any is left (Climate, Tube Warm-up); the labels after that claim are only used with it."""
    patches = P.ASFirmwarePatches
    pools = {"labels": None}   # reclaimed string IDs: the count depends on the firmware
    reclaim = inspect.getsource(patches.custom_patch_settings_reclaim_reminders)
    for names, table in re.findall(r"for var in \(([^)]*)\):\s*self\.custom_reclaim_g(\d)_var", reclaim):
        pools["g" + table] = len(re.findall(r"'\w+'", names))
    collect = inspect.getsource(patches.custom_patch_settings_collect_features)
    resources = {}
    for source, method in re.findall(r"\('(\w+)', self\.(\w+)\)", collect):
        body = inspect.getsource(getattr(patches, method))
        free = re.search(r"custom_claim_free_g8_var\(\s*\(([^)]*)\)", body)
        fixed, optional = (body[:free.start()], body[free.start():]) if free else (body, "")
        label = r"redefine_fw_string\(-1, \{0: '([^']*)'"
        used = {"g8": re.findall(r"custom_claim_g8_var\('(\w+)'", fixed),
                "g4": re.findall(r"custom_claim_g4_var\('(\w+)'", body),
                "labels": re.findall(label, fixed),
                "free_g8": (re.findall(r"'(\w+)'", free.group(1)), re.findall(label, optional)) if free else None}
        for spec in P.iter_patch_specs():   # the patch that activates this feature
            if spec.method == "patch_" + source or "add('%s')" % source in inspect.getsource(getattr(patches, spec.method)):
                resources[spec.option] = used
                break
    return resources, pools


class Tooltip:
    """Shows text from a callback after hovering a widget for a moment."""
    def __init__(self, widget, text):
        self.widget, self.text, self.tip, self.job = widget, text, None, None
        widget.bind("<Enter>", self.schedule, add="+")
        widget.bind("<Leave>", self.hide, add="+")
        widget.bind("<ButtonPress>", self.hide, add="+")

    def schedule(self, _):
        self.hide()
        self.job = self.widget.after(500, self.show)

    def show(self):
        text = self.text()
        if not text:
            return
        x, y = self.widget.winfo_rootx() + 20, self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        tk.Label(self.tip, text=text, justify="left", background="#ffffe8", relief="solid", borderwidth=1,
                 padx=6, pady=4).pack()
        self.tip.update_idletasks()   # keep it on the screen
        x = min(x, self.tip.winfo_screenwidth() - self.tip.winfo_reqwidth() - 8)
        if y + self.tip.winfo_reqheight() > self.tip.winfo_screenheight() - 40:
            y = self.widget.winfo_rooty() - self.tip.winfo_reqheight() - 4
        self.tip.wm_geometry("+%d+%d" % (x, y))

    def hide(self, _=None):
        if self.job:
            self.widget.after_cancel(self.job)
            self.job = None
        if self.tip:
            self.tip.destroy()
            self.tip = None


class OptionsParser(argparse.ArgumentParser):
    """Raises instead of printing and exiting: there is no console under pythonw."""
    def error(self, message):
        raise ValueError(message)


def options_parser():
    """Only the options of the patcher's CLI, with identical types (as parse_patch_args in lib/patch_config.py)."""
    options = OptionsParser(add_help=False, allow_abbrev=False)
    for action in P.build_argument_parser()._actions:
        if action.option_strings and not isinstance(action, argparse._HelpAction):
            options._add_action(copy(action))
    return options


def dest(option):
    return option.replace("-", "_")


def option_text(value):
    """A patch value as written on the command line: y, n or a number (85, 72.5)."""
    if value is True:
        return "y"
    if value is False:
        return "n"
    return "%g" % value


def wrap_lines(text):
    return textwrap.fill(text, 90)


def env_overrides():
    """Environment variables that override patch-airsense.config when patching (docs/patch_options.md)."""
    names = [name for name in AIR10_LEGACY_OPTIONS if os.environ.get(name) == "1"]
    if "PATCH_TARGET_RH" in os.environ:
        names.append("PATCH_TARGET_RH")
    if os.environ.get("AIR10_PATCH_ARGS", "").strip():
        names.append("AIR10_PATCH_ARGS")
    return names


def load_settings():
    try:
        with open(SETTINGS_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def build_command():
    """make binaries, through WSL on Windows (docs/guide/wsl_setup.md); None when make is not available."""
    if os.name == "nt":
        wsl = shutil.which("wsl.exe")
        return [wsl, "--cd", REPO, "--", "make", "binaries"] if wsl else None
    return ["make", "binaries"] if shutil.which("make") else None


WSL_TOOLS_HINT = "WSL needs openocd and usbutils: sudo apt install openocd usbutils (docs/guide/wsl_setup.md)."
LOG_HINTS = (   # output -> what to do about it, shown when the command ends
    ("run make binaries", "Click 'Build payloads' to run make binaries, then Patch again."),
    ("openocd: command not found", WSL_TOOLS_HINT),
    ("lsusb: command not found", WSL_TOOLS_HINT),
    ("Error: open failed", "OpenOCD found no adapter: check the programmer's USB cable and driver, or in WSL "
     "that usbipd shares it (docs/guide/openocd.md)."),
)


def find_openocd():
    """openocd on the PATH, else a known Windows install; empty when none is found."""
    found = shutil.which("openocd")
    if found:
        return found
    if os.name == "nt":
        for pattern in OPENOCD_GLOBS:
            hits = sorted(glob.glob(pattern), reverse=True)
            if hits:
                return os.path.normpath(hits[0])
    return ""


def python_exe():
    """python.exe, also when started through pythonw.exe, so the child has a console-less pipe-friendly stdout."""
    exe = sys.executable
    if os.path.basename(exe).lower() == "pythonw.exe":
        exe = os.path.join(os.path.dirname(exe), "python.exe")
    return exe


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.geometry("1280x900")
        self.parser = options_parser()
        self.specs = {spec.option: spec for spec in P.iter_patch_specs()}
        self.docs = patch_docs()
        self.resources, self.pools = menu_resources()
        self.enabled = {}   # option -> BooleanVar: patch selected
        self.values = {}    # option -> StringVar: parameter of a patch with a value (target RH)
        self.spins = {}     # option -> Spinbox of that parameter, disabled while the patch is unticked
        self.checks = {}    # option -> Checkbutton, bold when not at the built-in default
        self.extra = []     # other patcher options found in the config file, written back unchanged
        self.foreign = False      # the config file was not written by this window: ask before replacing it
        self.saved_tokens = None  # options in the config file, None when it could not be read
        self.loading = False
        self.proc = None
        self.done = ""          # log text when the running command succeeds
        self.flashing = False   # the running command writes the device flash: ask before stopping it
        self.hints = []         # LOG_HINTS matched by the running command's output
        self.lines = queue.Queue()

        bold = tkfont.nametofont("TkDefaultFont").copy()
        bold.configure(weight="bold")
        self.bold = bold   # the style only keeps the font name
        ttk.Style(self).configure("Changed.TCheckbutton", font=bold)

        panes = ttk.PanedWindow(self, orient="vertical")   # top: options, bottom: patch firmware and log
        panes.pack(fill="both", expand=True, padx=8, pady=8)
        top = ttk.Frame(panes)
        bottom = ttk.Frame(panes)
        panes.add(top, weight=3)
        panes.add(bottom, weight=2)

        area = ttk.Frame(top)
        area.pack(fill="both", expand=True)
        canvas = tk.Canvas(area, highlightthickness=0)
        scroll = ttk.Scrollbar(area, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        inner = ttk.Frame(canvas)
        window = canvas.create_window(0, 0, window=inner, anchor="nw")
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(window, width=e.width))

        def wheel(event):   # only scroll the options while the mouse is over them
            under = self.winfo_containing(event.x_root, event.y_root)
            if under is not None and str(under).startswith(str(canvas)):
                canvas.yview_scroll(-event.delta // 120, "units")
        self.bind_all("<MouseWheel>", wheel)

        usage = ttk.LabelFrame(inner, text="Custom settings menu", padding=(8, 4))   # the counter
        usage.grid(row=0, column=0, columnspan=2, sticky="we", pady=(0, 8), padx=(0, 4))
        self.usage = {}
        for key, _ in SLOTS:
            self.usage[key] = ttk.Label(usage)
            self.usage[key].pack(side="left", padx=(0, 24))
            Tooltip(self.usage[key], lambda k=key: self.usage_text(k))
        self.usage_note = ttk.Label(usage, foreground="#b36b00")   # settings dropped for lack of a free slot
        self.usage_note.pack(side="left")
        Tooltip(usage, lambda: self.usage_text(None))

        columns = [ttk.Frame(inner) for _ in range(2)]   # one LabelFrame per patch phase, in two columns
        for i, column in enumerate(columns):
            column.grid(row=1, column=i, sticky="nwe", padx=(0, 8) if i == 0 else (0, 4))
            inner.columnconfigure(i, weight=1, uniform="column")
        rows = [0, 0]
        for phase, specs in P.PATCH_PHASES:
            i = rows.index(min(rows))
            rows[i] += len(specs) + 2
            frame = ttk.LabelFrame(columns[i], text=phase, padding=8)
            frame.pack(fill="x", pady=(0, 8))
            frame.columnconfigure(0, weight=1)
            for spec in specs:
                self.add_option(frame, spec)

        bar = ttk.Frame(top)
        bar.pack(fill="x", pady=(8, 0))
        ttk.Button(bar, text="Save", command=self.save).pack(side="left")
        ttk.Button(bar, text="Reload", command=self.reload).pack(side="left", padx=(6, 0))
        ttk.Button(bar, text="Reset to defaults", command=self.reset).pack(side="left", padx=(6, 0))
        self.status = ttk.Label(bar, text="")
        self.status.pack(side="left", padx=12)
        line = ttk.Frame(top)
        line.pack(fill="x", pady=(6, 0))
        ttk.Label(line, text="Command line:").pack(side="left")
        self.cmdline = tk.StringVar()   # the saved options in CLI syntax, e.g. for AIR10_PATCH_ARGS
        ttk.Entry(line, textvariable=self.cmdline, state="readonly", font=("Consolas", 9)).pack(
            side="left", fill="x", expand=True, padx=(6, 0))

        settings = load_settings()
        sections = ttk.Frame(bottom)   # left: patch an image, right: flash it to the device
        sections.pack(fill="x")
        patch = ttk.LabelFrame(sections, text="Patch firmware", padding=8)
        patch.pack(side="left", fill="both", expand=True)
        patch.columnconfigure(1, weight=1)
        self.infile = tk.StringVar(value=settings.get("input", ""))
        self.outfile = tk.StringVar(value=settings.get("output") or DEFAULT_OUTPUT)
        self.verbose = tk.BooleanVar(value=settings.get("verbose", False))
        for row, (label, var, saving) in enumerate((("Input firmware:", self.infile, False),
                                                    ("Output firmware:", self.outfile, True))):
            ttk.Label(patch, text=label).grid(row=row, column=0, sticky="w", pady=(0, 4))
            ttk.Entry(patch, textvariable=var).grid(row=row, column=1, sticky="we", padx=6, pady=(0, 4))
            ttk.Button(patch, text="Browse...", command=lambda v=var, s=saving: self.browse(v, s)).grid(
                row=row, column=2, pady=(0, 4))
        actions = ttk.Frame(patch)
        actions.grid(row=2, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.patch_btn = ttk.Button(actions, text="Patch", command=self.patch)
        self.patch_btn.pack(side="left")
        self.build_btn = ttk.Button(actions, text="Build payloads (make binaries)", command=self.build)
        self.build_btn.pack(side="left", padx=(6, 0))
        Tooltip(self.build_btn, lambda: wrap_lines(
            "Compiles the payloads the patcher loads from build/ (graph, Custom VAuto, Tube Warm-up, ...). Needed "
            "once, and again after changing patches/. Runs: %s" % " ".join(build_command() or ["make", "binaries"])))
        self.stop_btn = ttk.Button(actions, text="Stop", command=self.stop, state="disabled")
        self.stop_btn.pack(side="left", padx=(6, 0))
        ttk.Checkbutton(actions, text="Verbose (per-patch details)", variable=self.verbose).pack(side="left", padx=12)

        flash = ttk.LabelFrame(sections, text="Flash device (SWD, OpenOCD)", padding=8)
        flash.pack(side="left", fill="both", expand=True, padx=(8, 0))
        flash.columnconfigure(1, weight=1)
        found = find_openocd()
        self.ocd_mode = tk.StringVar(value=settings.get("ocd_mode") or ("windows" if found else "wsl"))
        self.ocd_path = tk.StringVar(value=settings.get("ocd_path") or found)
        self.ocd_interface = tk.StringVar(value=settings.get("ocd_interface") or "stlink")
        self.flash_file = tk.StringVar(value=settings.get("flash_file") or self.outfile.get())
        self.backup = tk.StringVar(value=settings.get("backup") or DEFAULT_BACKUP)
        ttk.Label(flash, text="OpenOCD:").grid(row=0, column=0, sticky="w", pady=(0, 4))
        mode = ttk.Frame(flash)
        mode.grid(row=0, column=1, columnspan=2, sticky="w", padx=6, pady=(0, 4))
        for value, text in (("windows", "Windows openocd.exe"), ("wsl", "WSL (run-ocd.sh)")):
            ttk.Radiobutton(mode, text=text, value=value, variable=self.ocd_mode,
                            command=self.ocd_mode_changed).pack(side="left", padx=(0, 12))
        ttk.Label(mode, text="Adapter:").pack(side="left")
        ttk.Combobox(mode, textvariable=self.ocd_interface, values=("stlink", "jlink"), state="readonly",
                     width=7).pack(side="left", padx=(4, 0))
        ttk.Label(flash, text="openocd.exe:").grid(row=1, column=0, sticky="w", pady=(0, 4))
        self.ocd_entry = ttk.Entry(flash, textvariable=self.ocd_path)
        self.ocd_entry.grid(row=1, column=1, sticky="we", padx=6, pady=(0, 4))
        self.ocd_browse = ttk.Button(flash, text="Browse...", command=self.browse_openocd)
        self.ocd_browse.grid(row=1, column=2, pady=(0, 4))
        for row, (label, var, saving) in enumerate((("Firmware:", self.flash_file, False),
                                                    ("Backup (dump):", self.backup, True)), start=2):
            ttk.Label(flash, text=label).grid(row=row, column=0, sticky="w", pady=(0, 4))
            ttk.Entry(flash, textvariable=var).grid(row=row, column=1, sticky="we", padx=6, pady=(0, 4))
            ttk.Button(flash, text="Browse...", command=lambda v=var, s=saving: self.browse(v, s)).grid(
                row=row, column=2, pady=(0, 4))
        ocd_actions = ttk.Frame(flash)
        ocd_actions.grid(row=4, column=0, columnspan=3, sticky="w", pady=(8, 0))
        self.ocd_buttons = []
        for text, command, tip in (
                ("Check device", self.ocd_check, "Connects and reports whether the device runs stock or patched "
                 "firmware."),
                ("Dump backup", self.ocd_dump, "Saves the whole 1 MB flash to the backup file (dump). Do this "
                 "once on stock firmware before flashing: it is the only way to restore the device."),
                ("Flash", self.ocd_flash, "Writes the firmware file to the device and verifies it (flash_new). "
                 "Takes about 30 seconds; the device reboots afterwards."),
                ("Verify", self.ocd_verify, "Compares the device flash with the firmware file "
                 "(flash verify_bank)."),
                ("Reset", self.ocd_reset, "Restarts the device (reset run).")):
            button = ttk.Button(ocd_actions, text=text, command=command)
            button.pack(side="left", padx=(0, 6))
            Tooltip(button, lambda t=tip: wrap_lines(t))
            self.ocd_buttons.append(button)
        self.ocd_mode_changed()

        self.log = tk.Text(bottom, state="disabled", wrap="none", font=("Consolas", 10), height=10)
        self.log.pack(fill="both", expand=True, pady=(8, 0))
        self.log.tag_configure("fail", foreground="#b00020")
        self.log.tag_configure("warn", foreground="#b36b00")
        self.log.tag_configure("pass", foreground="#1b7f2a")

        self.load()
        overrides = env_overrides()
        if overrides:
            self.append("Note: %s set in the environment override %s when patching"
                        % (", ".join(overrides), os.path.basename(CONFIG_FILE)), "warn")
        self.after(100, self.pump)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def add_option(self, frame, spec):
        """A checkbox with the patch description and its option name; a patch with a value also gets a spinbox."""
        row = frame.grid_size()[1]
        text, _, hint = spec.description.partition(";")
        if spec.deprecated:
            hint = "deprecated, only applied with --force-deprecated: " + spec.deprecated
        self.enabled[spec.option] = tk.BooleanVar()
        check = ttk.Checkbutton(frame, text=text, variable=self.enabled[spec.option],
                                command=lambda: self.toggled(spec.option))
        check.grid(row=row, column=0, sticky="w")
        name = ttk.Label(frame, text="--" + spec.option, foreground="#888888", font=("Consolas", 9))
        name.grid(row=row, column=1, sticky="e", padx=(8, 0))
        for widget in (check, name):
            Tooltip(widget, lambda: self.tooltip_text(spec))
        self.checks[spec.option] = check
        if not spec.value_type and not hint:
            return
        sub = ttk.Frame(frame)
        sub.grid(row=row + 1, column=0, columnspan=2, sticky="w", padx=(22, 0), pady=(0, 2))
        if spec.value_type:   # only percentages today (patch-target-rh)
            self.values[spec.option] = tk.StringVar()
            self.values[spec.option].trace_add("write", self.refresh)
            self.spins[spec.option] = ttk.Spinbox(sub, from_=0, to=100, increment=1, width=6,
                                                  textvariable=self.values[spec.option])
            self.spins[spec.option].pack(side="left")
            ttk.Label(sub, text="%").pack(side="left", padx=(2, 8))
        if hint:
            ttk.Label(sub, text=hint.strip(), foreground="#666666").pack(side="left")

    def tooltip_text(self, spec):
        """Extended information on a patch: docs, default, dependencies and the menu items it adds."""
        wrap = wrap_lines
        what, page, more = self.docs.get(spec.option, ("", None, ""))
        parts = ["--%s (default: %s)" % (spec.option, "off" if spec.default is False else
                                         "on" if spec.default is True else option_text(spec.default))]
        parts += [wrap(text) for text in (what, more) if text]
        needs = P.PATCH_DEPENDENCIES.get(spec.option, ())
        needed_by = [other for other, deps in P.PATCH_DEPENDENCIES.items() if spec.option in deps]
        if needs:
            parts.append("Requires: " + ", ".join("--" + o for o in needs))
        if needed_by:
            parts.append("Required by: " + ", ".join("--" + o for o in needed_by))
        used = self.resources.get(spec.option)
        if used:
            counts = ", ".join("%s: %d" % (label.lower(), len(used[key])) for key, label in SLOTS if used[key])
            parts.append(wrap("Custom settings menu (with --patch-custom-settings): %s%s" % (
                "uses " + counts if counts else "shows stock menu items, uses no reclaimed slots",
                "; adds " + ", ".join(used["labels"]) if used["labels"] else "")))
            if used["free_g8"]:
                candidates, labels = used["free_g8"]
                parts.append(wrap("%s takes a free on/off setting (%s) left by the other patches, and is left out "
                                  "when none is free." % (", ".join(labels) or "One setting", ", ".join(candidates))))
        if page:
            parts.append("More: " + os.path.relpath(page, REPO).replace(os.sep, "/"))
        return "\n\n".join(parts)

    def menu_usage(self, values):
        """Reclaimed slots and labels used by the selected patches, plus the labels of settings left out for lack
        of a free on/off variable; None without custom settings. Free claims follow the fixed ones, like in the
        patcher, where those features run last."""
        if not values.get("patch-custom-settings"):
            return None
        used = {key: [] for key, _ in SLOTS}
        used["omitted"] = []
        selected = [resources for option, resources in self.resources.items() if values.get(option) is not False]
        for resources in selected:
            for key, _ in SLOTS:
                used[key] += resources[key]
        for resources in selected:
            if resources["free_g8"]:
                candidates, labels = resources["free_g8"]
                free = [var for var in candidates if var not in used["g8"]]
                if free:
                    used["g8"].append(free[0])
                    used["labels"] += labels
                else:
                    used["omitted"] += labels
        return used

    def usage_text(self, key):
        if key == "labels":
            return wrap_lines(
                "Menu labels are stored in string IDs reclaimed from the stock Reminders menu: its title and "
                "message strings plus the now unused names of the reclaimed variables. How many are free "
                "depends on the firmware; tick Verbose and patch to see 'free reclaimed str_id' in the log.")
        if key:
            table = {"g8": "g[8]", "g4": "g[4]"}[key]
            return wrap_lines(
                "%s variables reclaimed from the stock Reminders menu, one per added menu setting. Each feature "
                "claims fixed variables, so two features cannot share one. Free after patching: see "
                "'unused reclaimed %s vars' in the verbose log." % (table, table))
        return wrap_lines(
            "Patches with menu controls share the resources freed by removing the stock Reminders menu "
            "(docs/custom_settings.md). Counted only while --patch-custom-settings is ticked.")

    def update_usage(self, values):
        used = self.menu_usage(values)
        for key, label in SLOTS:
            if used is None:
                self.usage[key].config(text="%s: -" % label, foreground="#888888")
                continue
            pool = self.pools.get(key)
            over = pool is not None and len(used[key]) > pool
            self.usage[key].config(text="%s: %d%s" % (label, len(used[key]), " / %d" % pool if pool else ""),
                                   foreground="#b00020" if over else "")
        omitted = used["omitted"] if used else []
        self.usage_note.config(text="Left out, no free on/off setting: " + ", ".join(omitted) if omitted else "")

    def current(self):
        """option -> value as the patcher takes it: True/False, or the percentage of a patch with a value.
        Raises argparse.ArgumentTypeError for a value the patcher would refuse."""
        result = {}
        for option, spec in self.specs.items():
            on = self.enabled[option].get()
            result[option] = spec.value_type(self.values[option].get().strip()) if spec.value_type and on else on
        return result

    def tokens(self, values):
        """The options that differ from the built-in defaults, in CLI syntax, followed by the kept other options."""
        out = []
        for option, value in values.items():
            if value != self.specs[option].default:
                out += ["--" + option, option_text(value)]
        return out + self.extra

    def extra_tokens(self, ns):
        """Non-patch options set in the config file (--force-deprecated, --log-file, ...)."""
        tokens = []
        for action in self.parser._actions:
            if action.dest in map(dest, self.specs):
                continue
            value = getattr(ns, action.dest)
            if value != action.default:
                name = max(action.option_strings, key=len)
                tokens += [name] if action.nargs == 0 else [name, str(value)]
        return tokens

    def apply(self, values):
        self.loading = True
        for option, spec in self.specs.items():
            value = values[option]
            self.enabled[option].set(value is not False)
            if spec.value_type:   # an unticked patch keeps showing the default value
                self.values[option].set(option_text(value if value is not False else spec.default))
        self.loading = False
        self.refresh()

    def dirty(self):
        try:
            return self.tokens(self.current()) != self.saved_tokens
        except argparse.ArgumentTypeError:
            return True

    def refresh(self, *_):
        """Spinbox states, bold for options off their default, the command line and the title."""
        if self.loading:
            return
        for option, spin in self.spins.items():
            spin.config(state="normal" if self.enabled[option].get() else "disabled")
        try:
            values = self.current()
        except argparse.ArgumentTypeError as exc:
            self.cmdline.set("Invalid value: %s" % exc)
            return
        self.update_usage(values)
        for option, value in values.items():
            changed = value != self.specs[option].default
            self.checks[option].config(style="Changed.TCheckbutton" if changed else "TCheckbutton")
        tokens = self.tokens(values)
        self.cmdline.set(" ".join(shlex.quote(t) for t in tokens))
        self.title("%s - %s%s" % (TITLE, os.path.basename(CONFIG_FILE),
                                  " *" if tokens != self.saved_tokens else ""))

    def toggled(self, option):
        changed = []
        on = self.enabled[option].get()
        self.follow(option, on, changed)
        self.status.config(text="Also %s: %s" % ("enabled" if on else "disabled", ", ".join("--" + o for o in changed))
                           if changed else "")
        self.refresh()

    def follow(self, option, on, changed):
        """Ticking a patch ticks what it requires; unticking one unticks the patches that require it."""
        if on:
            related = P.PATCH_DEPENDENCIES.get(option, ())
        else:
            related = [other for other, needs in P.PATCH_DEPENDENCIES.items() if option in needs]
        for other in related:
            if self.enabled[other].get() != on:
                self.enabled[other].set(on)
                changed.append(other)
                self.follow(other, on, changed)

    def load(self):
        """Reads patch-airsense.config with the patcher's own option types; no file means the built-in defaults."""
        values = {option: spec.default for option, spec in self.specs.items()}
        self.extra, self.foreign, self.saved_tokens = [], False, []
        try:
            with open(CONFIG_FILE, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            text = None
            self.status.config(text="No %s yet: built-in defaults shown" % os.path.basename(CONFIG_FILE))
        except OSError as exc:
            text = ""
            self.saved_tokens, self.foreign = None, True
            messagebox.showerror(TITLE, "%s could not be read:\n\n%s" % (CONFIG_FILE, exc))
        if text:
            self.foreign = not text.startswith(HEADER)
            try:
                ns = self.parser.parse_args(shlex.split(text, comments=True))
                values = {option: getattr(ns, dest(option)) for option in self.specs}
                self.extra = self.extra_tokens(ns)
                self.status.config(text="Loaded %s" % CONFIG_FILE)
            except ValueError as exc:
                self.saved_tokens, self.foreign = None, True
                messagebox.showerror(TITLE, "%s could not be read:\n\n%s\n\nThe built-in defaults are shown; "
                                     "Save replaces the file." % (CONFIG_FILE, exc))
                self.status.config(text="Could not read %s" % os.path.basename(CONFIG_FILE))
        self.apply(values)
        if self.saved_tokens is not None:
            self.saved_tokens = self.tokens(values)
        self.refresh()

    def reload(self):
        if self.dirty() and not messagebox.askyesno(TITLE, "Discard the unsaved changes?"):
            return
        self.load()

    def reset(self):
        self.apply({option: spec.default for option, spec in self.specs.items()})
        self.status.config(text="Built-in defaults shown; Save writes them")

    def save(self):
        """Writes the options that differ from the built-in defaults, grouped by phase. False when not saved."""
        try:
            values = self.current()
            tokens = self.tokens(values)
            ns = self.parser.parse_args(tokens)   # the patcher's own checks, as a last line of defence
            P.validate_patch_dependencies(self.parser, ns)
        except (argparse.ArgumentTypeError, ValueError) as exc:
            messagebox.showerror(TITLE, "Not saved:\n\n%s" % exc)
            return False
        if self.foreign and os.path.exists(CONFIG_FILE) and not messagebox.askyesno(
                TITLE, "%s was not written by this window. Its comments and layout will be replaced.\n\n"
                "Save anyway?" % CONFIG_FILE):
            return False
        lines = [HEADER, "# Defaults: python/patch-airsense.py --help. Reference: docs/patch_options.md", ""]
        for phase, specs in P.PATCH_PHASES:
            group = [spec for spec in specs if values[spec.option] != spec.default]
            if group:
                lines.append("# " + phase)
                lines += ["--%s %s" % (spec.option, option_text(values[spec.option])) for spec in group]
                lines.append("")
        if self.extra:
            lines += ["# Other options", " ".join(shlex.quote(t) for t in self.extra), ""]
        if not tokens:
            lines += ["# All options at their built-in defaults.", ""]
        with open(CONFIG_FILE, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        self.foreign = False
        self.saved_tokens = tokens
        self.status.config(text="Saved %s" % CONFIG_FILE)
        self.refresh()
        return True

    def browse(self, var, saving):
        start = os.path.dirname(var.get()) or REPO
        kwargs = dict(initialdir=start, filetypes=[("Firmware image", "*.bin"), ("All files", "*.*")])
        path = (filedialog.asksaveasfilename(defaultextension=".bin", **kwargs) if saving
                else filedialog.askopenfilename(**kwargs))
        if path:
            var.set(os.path.normpath(path))

    def save_settings(self):
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump({"input": self.infile.get(), "output": self.outfile.get(),
                           "verbose": self.verbose.get(), "ocd_mode": self.ocd_mode.get(),
                           "ocd_path": self.ocd_path.get(), "ocd_interface": self.ocd_interface.get(),
                           "flash_file": self.flash_file.get(), "backup": self.backup.get()}, f, indent=2)
        except OSError:
            pass

    def append(self, text, tag=None):
        if tag is None:
            tag = ("fail" if "[ERROR]" in text or ": error:" in text or "Error:" in text or text.startswith("Traceback")
                   else "warn" if "[WARN]" in text or "[SKIP]" in text or text.startswith("Warn :")
                   else "pass" if "[OK]" in text else "")
        self.log.config(state="normal")
        self.log.insert("end", text + "\n", tag)
        self.log.see("end")
        self.log.config(state="disabled")

    def patch(self):
        """Saves the options, then runs patch-airsense.py, which reads them from patch-airsense.config."""
        infile, outfile = self.infile.get().strip(), self.outfile.get().strip()
        if not os.path.isfile(infile):
            messagebox.showerror(TITLE, "Choose the original firmware image to patch.")
            return
        if not outfile:
            messagebox.showerror(TITLE, "Choose where the patched image is written.")
            return
        if os.path.abspath(infile) == os.path.abspath(outfile):
            messagebox.showerror(TITLE, "The output would overwrite the original firmware image.")
            return
        if self.dirty() and not self.save():
            return
        self.save_settings()
        os.makedirs(os.path.dirname(os.path.abspath(outfile)), exist_ok=True)
        self.run("Patching %s -> %s" % (infile, outfile),
                 [python_exe(), "-u", PATCHER, infile, outfile, "PATCH", "--overwrite"] +
                 (["-v"] if self.verbose.get() else []),
                 "%s written" % outfile)
        for name in env_overrides():
            self.append("Note: %s from the environment overrides %s" % (name, os.path.basename(CONFIG_FILE)), "warn")

    def build(self):
        """Builds the compiled payloads the patcher loads from build/ (make binaries)."""
        command = build_command()
        if command is None:
            messagebox.showerror(TITLE, "make was not found. On Windows the payloads are built in WSL with make and "
                                 "arm-none-eabi-gcc; see docs/guide/wsl_setup.md.")
            return
        self.run("Building payloads: %s" % " ".join(command[command.index("make"):]), command, "payloads built")

    def run(self, title, command, done, flashing=False):
        """Runs a command with its output in the log; one at a time, all action buttons disabled meanwhile."""
        self.append("=== %s ===" % title)
        self.done, self.flashing, self.hints = done, flashing, []
        try:
            self.proc = subprocess.Popen(
                command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=REPO,
                env={**os.environ, "PYTHONIOENCODING": "utf-8"},
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except OSError as exc:
            self.append("Could not start %s: %s" % (command[0], exc), "fail")
            return
        threading.Thread(target=self.reader, args=(self.proc,), daemon=True).start()
        for button in self.action_buttons():
            button.config(state="disabled")
        self.stop_btn.config(state="normal")

    def action_buttons(self):
        return [self.patch_btn, self.build_btn] + self.ocd_buttons

    def stop(self, ask=True):
        if self.proc and self.proc.poll() is None:
            if self.flashing and ask and not messagebox.askyesno(
                    TITLE, "Flashing is in progress. Stopping now can leave the device without working firmware "
                    "until it is flashed again over SWD.\n\nStop anyway?", icon="warning"):
                return
            self.proc.terminate()

    def ocd_mode_changed(self):
        state = "normal" if self.ocd_mode.get() == "windows" else "disabled"
        self.ocd_entry.config(state=state)
        self.ocd_browse.config(state=state)

    def browse_openocd(self):
        path = filedialog.askopenfilename(initialdir=os.path.dirname(self.ocd_path.get()) or None,
                                          filetypes=[("OpenOCD", "openocd*.exe"), ("All files", "*.*")])
        if path:
            self.ocd_path.set(os.path.normpath(path))

    def tcl_path(self, path):
        """A file argument for the airsense.cfg commands: relative to the repo (OpenOCD runs there) when inside
        it, otherwise absolute, converted to a WSL path in WSL mode. Braced for Tcl. None, after telling the
        user, when WSL cannot reach the file."""
        path = os.path.realpath(path)   # also expands 8.3 names such as MICHIE~1, which WSL does not know
        try:
            rel = os.path.relpath(path, REPO)
        except ValueError:   # another drive
            rel = os.pardir
        if not rel.startswith(os.pardir):
            path = rel
        elif self.ocd_mode.get() == "wsl":
            result = subprocess.run(["wsl.exe", "--exec", "wslpath", "-a", path.replace("\\", "/")],
                                    capture_output=True, text=True,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if result.returncode != 0 or not result.stdout.strip():
                messagebox.showerror(TITLE, "WSL cannot reach %s. Move the file into the repository folder "
                                     "or use Windows openocd.exe." % path)
                return None
            path = result.stdout.strip()
        return "{%s}" % path.replace("\\", "/")

    def openocd_command(self, commands):
        """OpenOCD with tcl/airsense.cfg, which connects and reports the device state, then these commands and
        shutdown. WSL mode goes through run-ocd.sh, which also attaches the adapter with usbipd."""
        commands = list(commands) + ["shutdown"]
        interface = self.ocd_interface.get()
        if self.ocd_mode.get() == "wsl":
            wsl = shutil.which("wsl.exe")
            if not wsl:
                messagebox.showerror(TITLE, "wsl.exe was not found; choose Windows openocd.exe instead.")
                return None
            args = [wsl, "--cd", REPO, "--exec", "bash", "run-ocd.sh", "--interface", interface]
        else:
            exe = self.ocd_path.get().strip()
            if not os.path.isfile(exe):
                messagebox.showerror(TITLE, "Choose openocd.exe (OpenOCD for Windows, e.g. the xPack build), "
                                     "or use WSL.")
                return None
            args = [exe] + (["-f", "interface/stlink.cfg"] if interface == "stlink" else
                            ["-f", "interface/jlink.cfg", "-c", "transport select swd"])
            args += ["-f", "tcl/airsense.cfg"]
        for command in commands:
            args += ["-c", command]
        return args

    def run_openocd(self, title, commands, done, flashing=False):
        command = self.openocd_command(commands)
        if command:
            self.save_settings()
            self.run(title, command, done, flashing)

    def flash_image(self):
        """The firmware file to flash or verify, if it is a full 1 MB image."""
        path = self.flash_file.get().strip()
        if not os.path.isfile(path):
            messagebox.showerror(TITLE, "Choose the firmware image (e.g. the patched output).")
            return None
        if os.path.getsize(path) != FLASH_SIZE:
            messagebox.showerror(TITLE, "%s is %d bytes; a full 1 MB image (%d bytes) is needed."
                                 % (path, os.path.getsize(path), FLASH_SIZE))
            return None
        return path

    def ocd_check(self):
        self.run_openocd("Checking device", [], "device checked")

    def ocd_dump(self):
        path = self.backup.get().strip()
        if not path:
            messagebox.showerror(TITLE, "Choose where the backup is written.")
            return
        if os.path.exists(path) and not messagebox.askyesno(
                TITLE, "%s already exists. If it is your stock firmware dump, it is the only way to restore the "
                "device.\n\nOverwrite it?" % path, icon="warning", default="no"):
            return
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        target = self.tcl_path(path)
        if target:
            self.run_openocd("Dumping device flash to %s" % path, ["dump %s" % target], "%s written" % path)

    def ocd_flash(self):
        path = self.flash_image()
        if not path:
            return
        backup = self.backup.get().strip()
        if not os.path.isfile(backup) and not messagebox.askyesno(
                TITLE, "No backup found at %s.\n\nDumping the stock firmware before flashing is mandatory "
                "(docs/guide/openocd.md): it is the only way to restore the device. Flash anyway?" % backup,
                icon="warning", default="no"):
            return
        if not messagebox.askyesno(
                TITLE, "Flash %s to the device?\n\nThis replaces the whole firmware and takes about 30 seconds. "
                "Keep the device and the programmer connected until the log says Done." % path,
                icon="warning", default="no"):
            return
        image = self.tcl_path(path)
        if image:
            self.run_openocd("Flashing %s" % path, ["flash_new %s" % image],
                             "%s flashed and verified; the device reboots" % path, flashing=True)

    def ocd_verify(self):
        path = self.flash_image()
        image = path and self.tcl_path(path)
        if image:   # resume also after a mismatch, then report it
            self.run_openocd("Verifying device flash against %s" % path,
                             ["halt", "set r [catch {flash verify_bank 0 %s 0} msg]; resume; if {$r} {error $msg}"
                              % image], "device flash matches %s" % path)

    def ocd_reset(self):
        self.run_openocd("Resetting device", ["reset run"], "device reset")

    def reader(self, proc):
        for raw in proc.stdout:
            self.lines.put(raw.decode("utf-8", "replace").rstrip("\r\n"))
        self.lines.put(None)

    def pump(self):
        try:
            while True:
                line = self.lines.get_nowait()
                if line is None:
                    self.finished()
                else:
                    self.hints += [hint for needle, hint in LOG_HINTS if needle in line and hint not in self.hints]
                    self.append(line)
        except queue.Empty:
            pass
        self.after(100, self.pump)

    def finished(self):
        code = self.proc.wait()
        if code == 0:
            self.append("=== Done: %s ===" % self.done, "pass")
        else:
            self.append("=== Failed (exit code %d) ===" % code, "fail")
        for hint in self.hints:
            self.append(hint, "warn")
        self.proc, self.flashing = None, False
        for button in self.action_buttons():
            button.config(state="normal")
        self.stop_btn.config(state="disabled")

    def on_close(self):
        if self.dirty():
            answer = messagebox.askyesnocancel(TITLE, "Save the changes to %s?" % os.path.basename(CONFIG_FILE))
            if answer is None or (answer and not self.save()):
                return
        if self.flashing and not messagebox.askyesno(
                TITLE, "Flashing is in progress. Closing stops it and can leave the device without working "
                "firmware.\n\nClose anyway?", icon="warning", default="no"):
            return
        self.save_settings()
        self.stop(ask=False)
        self.destroy()

    def report_callback_exception(self, exc, value, tb):
        """There is no console under pythonw: show errors instead of losing them."""
        messagebox.showerror(TITLE, "".join(traceback.format_exception(exc, value, tb)))


def main():
    global P
    try:
        P = load_patcher()
    except Exception:
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(TITLE, "Could not load %s:\n\n%s" % (PATCHER, traceback.format_exc()))
        return 1
    App().mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
