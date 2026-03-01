# MomSurfFix2 Extension Configuration

## Installation

After installing the extension, you need to manually execute the configuration file:

### Method 1: Add to server.cfg
Add this line to your `cfg/server.cfg`:
```
exec sourcemod/momsurffix2.cfg
```

### Method 2: Execute manually
In server console:
```
exec sourcemod/momsurffix2.cfg
```

### Method 3: Auto-execute on map change
Add to `cfg/sourcemod/sourcemod.cfg`:
```
exec sourcemod/momsurffix2.cfg
```

## Note

Unlike SourcePawn plugins, C++ extensions do NOT auto-generate or auto-execute config files. You must manually execute the config file after installation.

## Configuration Options

See `momsurffix2.cfg` for all available cvars and their descriptions.
