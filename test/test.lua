utils = require 'utils'

p = cl.platforms()
info = {}
for i=1,#p do
	info[i] = p[1].info()
	local d = p[1].devices()
	info[i].devices = {}
	for j=1,#d do
		info[i].devices[j] = d[j].info()
	end
end
print(utils.dumplua(info))
utils.put(utils.dumplua(info), 'FILE:c:/temp/luaCL/out.txt')

ctx = cl.context(p[1].devices(), {platform = p[1]})
print(ctx, utils.dumplua(ctx.info()))
queue = ctx.queue(p[1].devices()[1], {"profiling_enable", "out_of_order_exec_mode_enable"})
print(queue, utils.dumplua(queue.info()))
buffer = ctx.buffer(("ABCD"):rep(10000))
print(buffer, utils.dumplua(buffer.info()))
-- "out_of_order_exec_mode_enable"  "profiling_enable" 