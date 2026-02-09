import sys
from ambuild2 import run

# 准备构建环境
builder = run.PrepareBuild(sourcePath=sys.path[0])

# 添加选项（移除nargs='?'，使用default使参数可选）
builder.options.add_option('--sdks', type='string', dest='sdks', default='csgo')
builder.options.add_option('--targets', type='string', dest='targets', default='x86')

# 配置并运行
builder.Configure()
