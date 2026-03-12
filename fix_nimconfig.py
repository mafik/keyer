"""Pre-build script: patch NimBLE nimconfig.h to add #ifndef guards,
preventing macro redefinition warnings from sdkconfig.h conflicts."""

import os
Import("env")

def patch_nimconfig(source, target, env):
    nimconfig = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"),
        env.subst("$PIOENV"),
        "NimBLE-Arduino", "src", "nimconfig.h"
    )
    if not os.path.isfile(nimconfig):
        return

    with open(nimconfig, "r") as f:
        content = f.read()

    if "/* patched by fix_nimconfig.py */" in content:
        return  # already patched

    # 1. Role macros: nimconfig.h defines them without value, sdkconfig.h
    #    defines them as 1.  Wrap in #ifndef so the sdkconfig.h definition wins.
    for role in ("CENTRAL", "OBSERVER", "PERIPHERAL", "BROADCASTER"):
        macro = f"CONFIG_BT_NIMBLE_ROLE_{role}"
        # Pattern: #define CONFIG_BT_NIMBLE_ROLE_XXX\n  (no value, at end of line)
        old = f"#define {macro}\n"
        new = f"#ifndef {macro}\n#define {macro}\n#endif\n"
        content = content.replace(old, new)

    # 2. Numeric macros that nimconfig.h defines unconditionally but
    #    sdkconfig.h may define with a different value.  Wrap in #ifndef.
    numeric_macros = [
        "CONFIG_BT_NIMBLE_ACL_BUF_COUNT",
        "CONFIG_BT_NIMBLE_ACL_BUF_SIZE",
        "CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT",
        "CONFIG_BT_NIMBLE_HCI_EVT_LO_BUF_COUNT",
        "CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM",
        "CONFIG_BT_NIMBLE_NVS_PERSIST",
        "CONFIG_BT_NIMBLE_SM_LEGACY",
        "CONFIG_BT_NIMBLE_SM_SC",
        "CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN",
        "CONFIG_BT_NIMBLE_HS_FLOW_CTRL",
        "CONFIG_BT_NIMBLE_HS_FLOW_CTRL_ITVL",
        "CONFIG_BT_NIMBLE_HS_FLOW_CTRL_THRESH",
        "CONFIG_BT_NIMBLE_HS_FLOW_CTRL_TX_ON_DISCONNECT",
        "CONFIG_BT_NIMBLE_HS_STOP_TIMEOUT_MS",
    ]
    for macro in numeric_macros:
        # Match #define MACRO <value>\n  where value is a number
        import re
        pattern = rf"(^#define {re.escape(macro)} (\d+)\n)"
        match = re.search(pattern, content, re.MULTILINE)
        if match:
            old_line = match.group(1)
            value = match.group(2)
            new_block = f"#ifndef {macro}\n#define {macro} {value}\n#endif\n"
            content = content.replace(old_line, new_block)

    # 3. Flag macros defined without value (like CONFIG_BT_ENABLED,
    #    CONFIG_BTDM_CONTROLLER_MODE_BLE_ONLY) — these already have
    #    #ifndef guards in nimconfig.h, so skip them.

    content = "/* patched by fix_nimconfig.py */\n" + content

    with open(nimconfig, "w") as f:
        f.write(content)
    print("*** nimconfig.h patched to suppress redefinition warnings")

# Run the patch immediately during script loading (pre: script runs before build)
patch_nimconfig(None, None, env)
