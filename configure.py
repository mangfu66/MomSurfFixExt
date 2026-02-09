import sys
from ambuild2 import run

# 准备构建环境
builder = run.PrepareBuild(sourcePath=sys.path[0])

# 添加选项（可选，根据需要扩展）
builder.options.add_option('--sdks', type='string', dest='sdks', nargs='?', default='csgo')
builder.options.add_option('--targets', type='string', dest='targets', nargs='?', default='x86')

# 配置并运行
builder.Configure()
