![UE 5.8](https://img.shields.io/badge/UE-5.8-darkgreen)

<h1 align="center">PCGEx | Toolset</h1>

<p align="center">
  <strong>Machine authoring for PCGEx data</strong><br>
  The JSON authoring hub (PCGExMediator) and the Properties and Collections authoring surfaces, for AI agents, console commands and tests.
</p>

<p align="center">
  <a href="https://pcgex.gitbook.io/pcgex/">Documentation</a> •
  <a href="https://discord.gg/mde2vC5gbE">Discord</a>
</p>

> [!IMPORTANT]
> **PCGEx Toolset is harnessed by the [PCGEx Assistant](https://github.com/PCGEx/PCGExAssistant) plugin.** The Assistant is what exposes this toolset to AI clients (Claude Code, Cursor, any MCP client); on its own, the toolset only offers console commands. Install **PCGExtendedToolkit**, **PCGEx Assistant** and **PCGEx Toolset** together.

## Requirements

- **Unreal Engine 5.8** (5.8 only: the toolset rides engine surface that does not exist in 5.7).
- **[PCGExtendedToolkit](https://github.com/Nebukam/PCGExtendedToolkit)**, the data this toolset authors.
- **[PCGEx Assistant](https://github.com/PCGEx/PCGExAssistant)**, the MCP harness. Follow its setup (Toolset Registry, Model Context Protocol server, client configuration) and the toolset's formats, actions and guides appear in the Assistant's tools and skill automatically.

Editor-only: nothing ships in cooked builds.

## What it provides

- **`PCGExMediator`**, the hub: JSON formats and object bindings for PCGEx models, a value dialect, atomic imports under one transaction, diagnostics, target resolution, the `pcgex.mediator.*` console commands and the dump commandlet (`-run=PCGExMediatorDump`) the documentation is rendered from.
- **Properties** and **Collections** authoring surfaces: property schemas, overrides and choices, Tuple nodes, asset collections and their entries, Distribute Tuple, with per-domain guides an agent reads at runtime.

## Domain plugins

The other PCGEx plugins get their own authoring surface as a sibling plugin that depends on this one and on the PCGEx Assistant:

- [PCGExToolsetValency](https://github.com/PCGEx/PCGExToolsetValency) for Valency.
- [PCGExToolsetClusterSketch](https://github.com/PCGEx/PCGExToolsetClusterSketch) for Cluster Sketch.

The vanilla PCGEx plugins know nothing of the toolsets; they only expose what the toolsets need.
