import ambuild2

builder = ambuild2.Builder('momsurffix_ext')
builder.options.cxx = 'clang++'
builder.options.arch = 'x86'
builder.options.sdks = ['csgo']  # 调整为您的游戏
builder.AddSource('src/momsurffix_ext.cpp')
builder.Build()
