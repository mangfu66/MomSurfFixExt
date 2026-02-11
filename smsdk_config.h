#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

// ============================================================================
// 扩展基本信息
// ============================================================================
#define SMEXT_CONF_NAME         "MomSurfFix Extension"
#define SMEXT_CONF_DESCRIPTION  "Ports Momentum Mod surf/ramp glitch fix to CS:GO/CSS"
#define SMEXT_CONF_VERSION      "1.0.0"
#define SMEXT_CONF_AUTHOR       "GAMMACASE (original)"
#define SMEXT_CONF_URL          "https://github.com/454369453/MomSurfFixExt"
#define SMEXT_CONF_LOGTAG       "MOMSURFFIX"
#define SMEXT_CONF_LICENSE      "GPL"
#define SMEXT_CONF_DATESTRING   __DATE__

// ============================================================================
// 核心功能模块 (只开启你真正需要的)
// ============================================================================
// 你的代码主要做 Detour (内存操作) 和读取 gamedata (游戏配置)
// 所以只需要下面这几个：

#define SMEXT_ENABLE_GAMECONF      // 必需：用于读取 .games.txt (gamedata)
#define SMEXT_ENABLE_MEMUTILS      // 必需：用于内存扫描、Hook 基础
#define SMEXT_ENABLE_GAMEHELPERS   // 必需：处理 Entity 转换

// 下面这些都是你的扩展没用到的，为了干净稳定，建议保持注释状态（不启用）：
// #define SMEXT_ENABLE_FORWARDSYS
// #define SMEXT_ENABLE_HANDLESYS
// #define SMEXT_ENABLE_PLAYERHELPERS
// #define SMEXT_ENABLE_DBMANAGER    // 不需要数据库
// #define SMEXT_ENABLE_TIMERSYS
// #define SMEXT_ENABLE_THREADER
// #define SMEXT_ENABLE_LIBSYS
// #define SMEXT_ENABLE_MENUS        // 不需要菜单
// #define SMEXT_ENABLE_ADTFACTORY
// #define SMEXT_ENABLE_PLUGINSYS
// #define SMEXT_ENABLE_ADMINSYS     // 不需要管理员权限检查
// #define SMEXT_ENABLE_TEXTPARSERS
// #define SMEXT_ENABLE_USERMSGS
// #define SMEXT_ENABLE_TRANSLATOR   // 不需要翻译文件
// #define SMEXT_ENABLE_ROOTCONSOLEMENU

// ============================================================================
// Metamod 设置 (绝对不要开启)
// ============================================================================
// #define SMEXT_CONF_METAMOD

#endif
