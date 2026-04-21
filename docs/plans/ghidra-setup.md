# Ghidra + GhidraMCP setup for collaborative disassembly

This guide gets Ghidra running with the MCP plugin so we can collaboratively analyze the Pro Controller patch RAM (`tmp0`) in real time.

## What's already done (in this repo)

- `tools/ghidramcp/` — downloaded the GhidraMCP 1.4 release, extracted the bridge script and the Ghidra extension zip
- `.mcp.json` — registers the `ghidra` MCP server for Claude Code (project-level)
- `.claude/settings.local.json` — auto-approves project MCP servers (`enableAllProjectMcpServers: true`)
- `.gitignore` — excludes `.claude/settings.local.json` (per convention; settings.local is per-user)

## What you need to do (one-time, ~15 min)

### 1. Install the GhidraMCP extension into Ghidra

1. Open Ghidra: **`open /Applications/ghidra_11.4.2_PUBLIC/ghidraRun`** (or just `/Applications/ghidraRun` alias)
2. Ghidra opens to its **Project Window**
3. Menu: **`File` → `Install Extensions`**
4. Click the **`+`** icon (top-right of dialog)
5. Navigate to and select: `/Users/alex/development/switch2controller/tools/ghidramcp/GhidraMCP-1-4.zip`
6. Confirm — `GhidraMCP` should appear checked in the extensions list
7. Click **OK**, then **restart Ghidra** when prompted

### 2. Verify the plugin is enabled

After Ghidra restarts:

1. **Open or create a project** (you'll need a project to use Ghidra's Code Browser tool, where the plugin lives)
2. Once a Code Browser is open: **`File` → `Configure`**
3. Click **`Developer`** category
4. Check that **`GhidraMCPPlugin`** is listed and enabled (checkbox checked)
5. **`Edit` → `Tool Options`** → `GhidraMCP HTTP Server` — verify port is `8080` (default, matches our `.mcp.json`)

### 3. Import `tmp0` for analysis

1. In the project window: **`File` → `Import File`**
2. Select `/Users/alex/development/switch2controller/references/mfro-switch-controller-testing/tmp0`
3. Import dialog appears. Configure:
   - **Format**: `Raw Binary`
   - **Language**: click `...` → search `ARM:LE:32:Cortex` → select `ARM:LE:32:Cortex (default)` (Little Endian, 32-bit, Cortex-M variant)
   - **Block Name**: `patch_ram`
   - **Base Address**: `0x10000` (matches SPI offset, matches mfro's analysis)
   - Leave other fields default
4. Click **OK**
5. Ghidra prompts about analysis — click **Yes**
6. In the Auto Analysis dialog, defaults are usually fine. Important checks:
   - ✅ ARM Aggressive Instruction Finder (helps with Thumb)
   - ✅ Decompiler Parameter ID
   - Click **Analyze** — takes 30-90 seconds for ~85 KB binary
7. When analysis finishes, Ghidra's Code Browser shows the disassembled patch RAM

### 4. Verify the MCP server is reachable

With Ghidra running and the Code Browser open:

```bash
curl -s http://127.0.0.1:8080/methods | head -10
```

Expected: a list of function names from `tmp0`. If you get connection refused, the plugin isn't loaded — check step 2.

## Verifying Claude Code sees the MCP

After completing steps above, **restart Claude Code** (or the MCP won't be picked up). Then in a Claude Code session in this directory, ask Claude to list MCP tools — it should show `mcp__ghidra__*` tools (decompile_function, list_methods, etc.).

If Ghidra MCP tools don't appear, check:
- `~/Library/Logs/Claude Code/` for connection errors
- That `/Users/alex/.local/bin/uv` exists and is executable (`uv --version`)
- That the bridge script runs manually: `/Users/alex/.local/bin/uv run --quiet /Users/alex/development/switch2controller/tools/ghidramcp/GhidraMCP-release-1-4/bridge_mcp_ghidra.py --help`

## Once setup is done

Tell me "Ghidra is running with `tmp0` loaded" and I'll start the disassembly investigation. Likely first tasks:

1. List all functions, identify entry point and any with recognizable name patterns
2. Find the `BLE ACL buffer allocation fail` string and trace its xrefs
3. Find all `bl` instructions targeting addresses outside `0x10000-0x27FFF` (= ROM calls)
4. Identify the OTA Signature Magic generator function
