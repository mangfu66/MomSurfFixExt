import os
import sys
from ambuild2 import Configurator

cfg = Configurator()
cfg.read_project_file('AMB.conf')

if not cfg.sdk_root:
    sys.exit("Error: sdk_root not set in AMB.conf")
if not cfg.hl2sdk_root:
    sys.exit("Error: hl2sdk_root not set in AMB.conf")
if not cfg.mmsource_root:
    sys.exit("Error: mmsource_root not set in AMB.conf")

# SourceMod public headers
cfg.add_include(os.path.join(cfg.sdk_root, 'public'))
cfg.add_include(os.path.join(cfg.sdk_root, 'public', 'extensions'))
cfg.add_include(os.path.join(cfg.sdk_root, 'public', 'sourcepawn'))

# HL2 SDK shared headers
cfg.add_include(os.path.join(cfg.hl2sdk_root, 'public'))
cfg.add_include(os.path.join(cfg.hl2sdk_root, 'public', 'tier0'))
cfg.add_include(os.path.join(cfg.hl2sdk_root, 'public', 'tier1'))
cfg.add_include(os.path.join(cfg.hl2sdk_root, 'public', 'vstdlib'))
cfg.add_include(os.path.join(cfg.hl2sdk_root, 'public', 'game', 'shared'))

# MetaMod core
cfg.add_include(os.path.join(cfg.mmsource_root, 'core'))
cfg.add_include(os.path.join(cfg.mmsource_root, 'core', 'sourcehook'))

# Library paths
lib_root = os.path.join(cfg.hl2sdk_root, 'lib')
if cfg.platform == 'linux':
    lib_root = os.path.join(lib_root, 'linux')
cfg.add_libpath(lib_root)

# Preprocessor defines
cfg.add_define('_CRT_SECURE_NO_DEPRECATE')
cfg.add_define('_CRT_NONSTDC_NO_DEPRECATE')
cfg.add_define('SOURCEMOD_BUILD')
cfg.add_define('POSIX')
cfg.add_define('LINUX')

# Output file
cfg.output_file = cfg.project_name + '.ext.so'

# Generate Makefile
cfg.generate_makefile()
print("Configuration complete – generated Makefile")
