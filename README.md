# RepSeedDumper

Runtime UE replication layout seed extractor, built on [Dumper-7](https://github.com/Encryqed/Dumper-7).

## Output

Produces `replication_seed.json` containing per-class replication data:

- handles - RepLayout handle tree with 1-based indices, property types, enum info, and struct inner expansion
- net_fields - Ordered list of replicated properties and RPCs used for ClassNetCache field index resolution

Only native C++ classes are included.

## Build

```
git clone --recurse-submodules https://github.com/Mokocoder/RepSeedDumper.git
```

Open `RepSeedDumper.sln` and build x64 Release. Output is a DLL.

## Usage

Inject the built DLL into a running UE process. Output is written to the generated SDK folder alongside the standard Dumper-7 output.

Press F6 to unload.

## Structure

```
src/
  main.cpp                 Entry point (DllMain + MainThread)
  RepLayoutGenerator.h     Generator class declaration
  RepLayoutGenerator.cpp   RepLayout extraction logic
  StructFlagsOffset.h      Runtime detection of UScriptStruct::StructFlags offset
Dumper-7/                  Submodule (upstream, unmodified)
```

## How it works

1. Dumper-7 initializes the engine core (GObjects, GNames, offsets)
2. `InitStructFlagsOffset()` probes known structs (Vector, Rotator, Guid, LinearColor) to locate `UScriptStruct::StructFlags` in memory
3. `RepLayoutGenerator::Generate()` walks the ObjectArray, builds the inheritance chain for each native class, and expands replicated properties into a handle tree matching UE's `FRepLayout` wire format
4. Structs with `NetSerializeNative` are emitted as opaque handles; structs without it are flattened into their sub-fields
