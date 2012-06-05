#include "CL/cl.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <string.h>
#include <assert.h>
#include <new>

typedef void (*push_t)(lua_State*L, const void* value, size_t size);

enum eInfoTable
{
	IT_PLATFORM, IT_DEVICE, IT_CONTEXT, IT_QUEUE, 
	IT_MEM, IT_IMAGE, IT_SAMPLER, IT_PROGRAM, 
	IT_PROGRAM_BUILD, IT_KERNEL, IT_WORKGROUP, IT_EVENT, 
	IT_PROFILING, IT_MAX
};
enum enumTypes
{
	EBT_DEVICE_TYPE, EBT_DEVICE_FP_CONFIG, EBT_DEVICE_MEM_CACHE_TYPE, EBT_DEVICE_LOCAL_MEM_TYPE, 
	EBT_DEVICE_EXEC_CAPABILITIES, EBT_COMMAND_QUEUE_PROPERTIES, EBT_CONTEXT_PROPERTIES, 
	EBT_DEVICE_PARTITION_PROPERTY, EBT_DEVICE_AFFINITY_DOMAIN, EBT_MEM_FLAGS, 
	EBT_MEM_MIGRATION_FLAGS, EBT_CHANNEL_ORDER, EBT_CHANNEL_TYPE, EBT_MEM_OBJECT_TYPE, 
	EBT_ADDRESSING_MODE, EBT_FILTER_MODE, EBT_MAP_FLAGS, EBT_PROGRAM_BINARY_TYPE, 
	EBT_BUILD_STATUS, EBT_KERNEL_ARG_ADDRESS_QUALIFIER, EBT_KERNEL_ARG_ACCESS_QUALIFIER, 
	EBT_KERNEL_ARG_TYPE_QUALIFIER, EBT_COMMAND_TYPE, EBT_COMMAND_EXECUTION_STATUS, EBT_BUFFER_CREATE_TYPE, 
};

struct info_list_t
{
	int id;
	const char* name;
	push_t pushFct;
};

struct error_info_t
{
	int id;
	const char* name;
};

struct enum_list_t
{
	enumTypes type_id;
	int value;
	const char* name;
};

static int getInfoTable(const info_list_t*& pinfo, eInfoTable info_table);
static int getEnumTable(const enum_list_t*& ptable, enumTypes enum_type);
static void error_check(lua_State* L, int error_code);
static void pushEnum(lua_State*L, const void* ptr, size_t size, enumTypes enum_type);
static void pushBitField(lua_State*L, const void* ptr, size_t size, enumTypes enum_type);

template<class obj_t, class param_t> static obj_t* pushNewObject(lua_State*L, param_t param)
{
	obj_t* obj = new(lua_newuserdata(L, sizeof(obj_t))) obj_t(param);
	obj->Retain();
	obj->Register(L);
	return obj;
}
template<class T> static void push(lua_State*L, const void* ptr, size_t size) { assert(size == sizeof(T)); lua_pushnumber(L, (lua_Number)*(const T*)ptr); }
template<class T> static void pushArray(lua_State*L, const void* ptr, size_t size) 
{ 
	int nbelem = (int)size/sizeof(T);
	const T* data = (const T*)ptr;
	lua_createtable(L, nbelem, 0);
	for(int i=0;i<nbelem;i++)
	{
		push<T>(L, data+i, sizeof(T));
		lua_rawseti(L, -2, i+1);
	}
}
template<enumTypes type> static void pushEnumArray(lua_State*L, const void* ptr, size_t size) 
{ 
	int nbelem = (int)size/sizeof(cl_uint);
	const cl_uint* data = (const cl_uint*)ptr;
	lua_createtable(L, nbelem, 0);
	for(int i=0;i<nbelem;i++)
	{
		pushEnum(L, data+i, sizeof(cl_uint), type);
		lua_rawseti(L, -2, i+1);
	}
}

template<> static void push<char[]>(lua_State*L, const void* ptr, size_t size) { lua_pushstring(L, (const char*)ptr); }
template<> static void push<bool>(lua_State*L, const void* ptr, size_t size) { lua_pushboolean(L, *(const cl_bool*)ptr); }
template<> static void push<void*>(lua_State*L, const void* ptr, size_t size) { lua_pushlightuserdata(L, *(void**)ptr); }

template<enumTypes enum_type> static void pushEnum(lua_State*L, const void* ptr, size_t size) { pushEnum(L, ptr, size, enum_type); }
template<enumTypes enum_type> static void pushBitField(lua_State*L, const void* ptr, size_t size) { pushBitField(L, ptr, size, enum_type); }

template<> static void push<cl_image_format>(lua_State*L, const void* ptr, size_t size) 
{ 
	const cl_image_format* pimg = (const cl_image_format*)ptr;
	lua_createtable(L, 0, 2);
	pushEnum<EBT_CHANNEL_ORDER>(L, &pimg->image_channel_order, sizeof(pimg->image_channel_order));
	lua_setfield(L, -2, "order");
	pushEnum<EBT_CHANNEL_ORDER>(L, &pimg->image_channel_data_type, sizeof(pimg->image_channel_data_type));
	lua_setfield(L, -2, "data_type");
}

template<class id_t, class get_info_t>
static int push_info(lua_State* L, id_t id, eInfoTable info_table, get_info_t get_info_fct)
{
	const info_list_t* info_list;
	int nb = getInfoTable(info_list, info_table);
	lua_createtable(L, 0, nb);
	int top = lua_gettop(L);
	for(int i=0;i<nb;i++)
	{
		size_t size;
		error_check(L, get_info_fct(id, info_list[i].id, 0, NULL, &size));
		void* pinfo = lua_newuserdata(L, size);
		error_check(L, get_info_fct(id, info_list[i].id, size, pinfo, NULL));
		info_list[i].pushFct(L, pinfo, size);
		lua_setfield(L, top, info_list[i].name);
		lua_settop(L, top);
	}
	return 1;
}

template<class id_t, class get_info_t>
static int push_info(lua_State* L, id_t id, eInfoTable info_table, get_info_t get_info_fct, cl_device_id device)
{
	const info_list_t* info_list;
	int nb = getInfoTable(info_list, info_table);
	lua_createtable(L, 0, nb);
	int top = lua_gettop(L);
	for(int i=0;i<nb;i++)
	{
		size_t size;
		error_check(L, get_info_fct(id, device, info_list[i].id, 0, NULL, &size));
		void* pinfo = lua_newuserdata(L, size);
		error_check(L, get_info_fct(id, device, info_list[i].id, size, pinfo, NULL));
		info_list[i].pushFct(L, pinfo, size);
		lua_setfield(L, top, info_list[i].name);
		lua_settop(L, top);
	}
	return 1;
}

static void pushEnum(lua_State*L, const void* ptr, size_t size, enumTypes enum_type)
{
	cl_uint val = *(const cl_uint*)ptr;
	const enum_list_t* ptable;
	int nb = getEnumTable(ptable, enum_type);
	for(int i=0;i<nb;i++)
	{
		if(val == ptable[i].value)
		{
			lua_pushstring(L, ptable[i].name);
			return;
		}
	}
	luaL_error(L, "unknown enum value = %d", val);
}

static void pushBitField(lua_State*L, const void* ptr, size_t size, enumTypes enum_type)
{
	cl_bitfield val = *(const cl_bitfield*)ptr;
	int cnt = 0;
	luaL_Buffer buf;
	luaL_buffinit(L, &buf);
	const enum_list_t* ptable;
	int nb = getEnumTable(ptable, enum_type);
	for(int i=0;i<nb;i++)
	{
		if((val & ptable[i].value) == ptable[i].value)
		{
			if(cnt++)
				luaL_addstring(&buf, ", ");
			luaL_addstring(&buf, ptable[i].name);
		}
	}
	luaL_pushresult(&buf);
}

void pushBinaries(lua_State*L, const void* ptr, size_t size)
{
	int nbelem = (int)size/sizeof(unsigned char*);
	const char** data = (const char**)ptr;
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_createtable(L, nbelem, 0);
	int top = lua_gettop(L);
	for(int i=0;i<nbelem;i++)
	{
		lua_getfield(L, -2, "binary_sizes");
		size_t size = (size_t)luaL_checknumber(L, -1);
		lua_settop(L, top);
		lua_pushlstring(L, data[i], size);
		lua_rawseti(L, -2, i+1);
	}
	lua_replace(L, -2);
}

int GetEnumValue(lua_State* L, enumTypes enum_type, const char* str)
{
	const enum_list_t* ptable;
	int len = getEnumTable(ptable, enum_type);
	for(int i=0;i<len;i++)
		if(strcmp(ptable[i].name, str) == 0)
			return ptable[i].value;
	luaL_error(L, "enumeration value '%s' not found", str);
	return 0;
}
static void context_info(lua_State* L)
{
#if 0
	cl_device_id device;
	push_info(L, to_object<cl_command_queue>(L), IT_QUEUE, clGetCommandQueueInfo);
	push_info(L, to_object<cl_mem>(L), IT_MEM, clGetMemObjectInfo);
	push_info(L, to_object<cl_mem>(L), IT_IMAGE, clGetImageInfo);
	push_info(L, to_object<cl_sampler>(L), IT_SAMPLER, clGetSamplerInfo);
	push_info(L, to_object<cl_program>(L), IT_PROGRAM, clGetProgramInfo);
	push_info(L, to_object<cl_program>(L), IT_PROGRAM_BUILD, clGetProgramBuildInfo, device);
	push_info(L, to_object<cl_kernel>(L), IT_KERNEL, clGetKernelInfo);
	push_info(L, to_object<cl_kernel>(L), IT_WORKGROUP, clGetKernelWorkGroupInfo, device);
	push_info(L, to_object<cl_event>(L), IT_EVENT, clGetEventInfo);
	push_info(L, to_object<cl_event>(L), IT_PROFILING, clGetEventProfilingInfo);
#endif
	//return push_info(L, to_object<cl_context>(L), IT_CONTEXT, clGetContextInfo);
}

class CLObject
{
public:
	typedef int (CLObject::*lua_method)(lua_State* L);
	virtual void Retain() {}
	virtual void Release() {}
	virtual int GetInfo(lua_State* L) = 0;
	static int CallMethod(lua_State* L)
	{
		CLObject* obj = (CLObject*)lua_touserdata(L, lua_upvalueindex(1));
		lua_method fct = *(lua_method*)lua_touserdata(L, lua_upvalueindex(2));
		// If called with the : syntax, remove extra self parameter
		if(lua_touserdata(L, 1) == obj)
			lua_remove(L, 1);
		return (obj->*fct)(L);
	}
	static CLObject* CheckObject(lua_State* L, int idx, const char* name)
	{
		luaL_checktype(L, idx, LUA_TUSERDATA);
		CLObject* obj = (CLObject*)lua_touserdata(L, idx);
		lua_getmetatable(L, idx);
		if(lua_type(L, -1) != LUA_TTABLE)
			luaL_error(L, "expected OpenCL %s object for argument %d, found unknown userdata", name, idx);
		lua_getfield(L, -1, "__metatable");
		if(lua_type(L, -1) != LUA_TSTRING)
			luaL_error(L, "expected OpenCL %s object for argument %d, found unknown userdata", name, idx);
		if(strcmp(lua_tostring(L, -1), "OpenCL metatable"))
			luaL_error(L, "expected OpenCL %s object for argument %d, found unknown userdata", name, idx);
		if(strcmp(obj->GetClassName(), name))
			luaL_error(L, "expected OpenCL %s object for argument %d, found %s object", name, idx, obj->GetClassName());
		lua_pop(L, 2);
		return obj;
	}
	void AddMethod(lua_State* L, lua_method fct, const char* name) 
	{
		lua_pushlightuserdata(L, this);
		lua_method* ud = (lua_method*)lua_newuserdata(L, sizeof fct);
		*ud = fct;
		lua_pushcclosure(L, CallMethod, 2); 
		lua_setfield(L, -2, name); 
	}
	void Register(lua_State* L)
	{
		lua_createtable(L, 0, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		lua_pushliteral(L, "OpenCL metatable");
		lua_setfield(L, -2, "__metatable");
		AddMethods(L);
		lua_setmetatable(L, -2);
	}
	virtual const char* GetClassName() = 0;
	int ToString(lua_State* L) { lua_pushfstring(L, "OpenCL %s (%p)", GetClassName(), this); return 1; }
	int GC(lua_State* L) { Release(); return 0; }
	virtual void AddMethods(lua_State* L)
	{
		AddMethod(L, &CLObject::GetInfo, "info");
		AddMethod(L, &CLObject::ToString, "__tostring");
		AddMethod(L, &CLObject::GC, "__gc");
	}
private:
};

class CLDevice : public CLObject
{
public:
	CLDevice(cl_device_id id) : Handle(id) {}
#ifdef CL_VERSION_1_2
	virtual void Retain() { clRetainDevice(Handle); }
	virtual void Release() { clReleaseDevice(Handle); }
#endif
	operator cl_device_id const() { return Handle; }
	virtual const char* GetClassName() { return "device"; }
	virtual int GetInfo(lua_State* L) { return push_info(L, Handle, IT_DEVICE, clGetDeviceInfo); }
private:
	cl_device_id Handle;
};

class CLPlatform : public CLObject
{
public:
	CLPlatform(cl_platform_id id) : Handle(id) {}
	virtual int GetInfo(lua_State* L) { return push_info(L, Handle, IT_PLATFORM, clGetPlatformInfo); }
	virtual const char* GetClassName() { return "platform"; }
	int GetDevices(lua_State* L)
	{
		cl_uint nb;
		error_check(L, clGetDeviceIDs(Handle, CL_DEVICE_TYPE_ALL, 0, NULL, &nb));
		cl_device_id* ids = (cl_device_id*) lua_newuserdata(L, nb * sizeof(cl_device_id));
		error_check(L, clGetDeviceIDs(Handle, CL_DEVICE_TYPE_ALL, nb, ids, NULL));
		lua_createtable(L, nb, 0);
		for(cl_uint i=0;i<nb;i++)
		{
			pushNewObject<CLDevice>(L, ids[i]);
			lua_rawseti(L, -2, i+1);
		}
		return 1;
	}
	virtual void AddMethods(lua_State* L)
	{
		CLObject::AddMethods(L);
		AddMethod(L, (lua_method)&CLPlatform::GetDevices, "devices");
	}
private:
	cl_platform_id Handle;
};

class CLContext : public CLObject
{
public:
	CLContext(cl_context id) : Handle(id) {}
	virtual void Retain() { clRetainContext(Handle); }
	virtual void Release() { clReleaseContext(Handle); }
	operator cl_context const() { return Handle; }
	virtual const char* GetClassName() { return "context"; }
	virtual int GetInfo(lua_State* L) { return push_info(L, Handle, IT_CONTEXT, clGetContextInfo); }
private:
	cl_context Handle;
};

template<> static void push<cl_platform_id>(lua_State*L, const void* ptr, size_t size) { pushNewObject<CLPlatform>(L, *(const cl_platform_id*)ptr); }
template<> static void push<cl_device_id>(lua_State*L, const void* ptr, size_t size) { pushNewObject<CLDevice>(L, *(const cl_device_id*)ptr); }
template<> static void push<cl_context>(lua_State*L, const void* ptr, size_t size) { lua_pushlightuserdata(L, *(void**)ptr); }
template<> static void push<cl_mem>(lua_State*L, const void* ptr, size_t size) { lua_pushlightuserdata(L, *(void**)ptr); }
template<> static void push<cl_program>(lua_State*L, const void* ptr, size_t size) { lua_pushlightuserdata(L, *(void**)ptr); }
template<> static void push<cl_command_queue>(lua_State*L, const void* ptr, size_t size) { lua_pushlightuserdata(L, *(void**)ptr); }

static int cl_platforms(lua_State* L)
{
	cl_uint nb;
	error_check(L, clGetPlatformIDs(0, NULL, &nb));
    cl_platform_id* ids = (cl_platform_id*) lua_newuserdata(L, nb * sizeof(cl_platform_id));
	error_check(L, clGetPlatformIDs(nb, ids, NULL));
	lua_createtable(L, nb, 0);
	for(cl_uint i=0;i<nb;i++)
	{
		pushNewObject<CLPlatform>(L, ids[i]);
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}

static void check_type(lua_State* L, int idx, int types)
{
	int t = lua_type(L, idx);
	if((1 << t) & types)
		return;
	luaL_Buffer buf;
	luaL_buffinit(L, &buf);
	int cnt = 0;
	for(int i=0;i<LUA_NUMTAGS;i++)
		if(types & (1<<i))
		{
			if(cnt++)
				luaL_addstring(&buf, " or ");
			luaL_addstring(&buf, lua_typename(L, i));
		}
	luaL_pushresult(&buf);
	luaL_error(L, "expected type %s for argument %d, got %s", lua_tostring(L, -1), idx, lua_typename(L, t));
}
static int cl_new_context(lua_State* L)
{
	cl_int err;
	cl_context context;
	cl_context_properties *properties = NULL;
	lua_settop(L, 3);
	check_type(L, 1, 1<<LUA_TTABLE|1<<LUA_TSTRING);
	check_type(L, 2, 1<<LUA_TTABLE|1<<LUA_TNIL);
	if(lua_type(L, 2) == LUA_TTABLE)
		; // TODO
	if(lua_type(L, 1) == LUA_TTABLE)
	{
		size_t nb = lua_rawlen(L, 1);
		cl_device_id* devices = (cl_device_id*)lua_newuserdata(L, nb*sizeof(cl_device_id));
		int top = lua_gettop(L);
		for(size_t i=0;i<nb;i++)
		{
			lua_rawgeti(L, 1, (int)i+1);
			CLDevice* dev = (CLDevice*)CLObject::CheckObject(L, top+1, "device");
			devices[i] = *dev;
			lua_settop(L, top);
		}
		context = clCreateContext(properties, (cl_uint)nb, devices, NULL, NULL, &err);
	}
	else
	{
		cl_device_type devtype = GetEnumValue(L, EBT_DEVICE_TYPE, lua_tostring(L, 1));
		context = clCreateContextFromType(properties, devtype, NULL, NULL, &err);
	}
	error_check(L, err);
	pushNewObject<CLContext>(L, context)->Release();
	return 1;
}
static const luaL_Reg cllib[] = 
{
	{ "platforms",   cl_platforms},
	{ "context",     cl_new_context},
	{ NULL, NULL}
};

extern "C" int luaopen_cl(lua_State* L)
{
	luaL_newlib(L, cllib);
	return 1;
}

#ifdef CL_VERSION_1_1
#define E1_1(a,b) { a,b },
#define V1_1(a,b,c) { a,b,c },
#else
#define E1_1(a,b)
#define V1_1(a,b,c)
#endif
#ifdef CL_VERSION_1_2
#define E1_2(a,b) { a,b },
#define V1_2(a,b,c) { a,b,c },
#else
#define E1_2(a,b)
#define V1_2(a,b,c)
#endif
#define E1_0(a,b) { a,b },
#define V1_0(a,b,c) { a,b,c },

// Big tables below
static const error_info_t error_info_list[] = 
{
	E1_0( CL_DEVICE_NOT_FOUND,                          "device not found"  )
	E1_0( CL_DEVICE_NOT_AVAILABLE,                      "device not available"  )
	E1_0( CL_COMPILER_NOT_AVAILABLE,                    "compiler not available"  )
	E1_0( CL_MEM_OBJECT_ALLOCATION_FAILURE,             "mem object allocation failure"  )
	E1_0( CL_OUT_OF_RESOURCES,                          "out of resources"  )
	E1_0( CL_OUT_OF_HOST_MEMORY,                        "out of host memory"  )
	E1_0( CL_PROFILING_INFO_NOT_AVAILABLE,              "profiling info not available"  )
	E1_0( CL_MEM_COPY_OVERLAP,                          "mem copy overlap"  )
	E1_0( CL_IMAGE_FORMAT_MISMATCH,                     "image format mismatch"  )
	E1_0( CL_IMAGE_FORMAT_NOT_SUPPORTED,                "image format not supported"  )
	E1_0( CL_BUILD_PROGRAM_FAILURE,                     "build program failure"  )
	E1_0( CL_MAP_FAILURE,                               "map failure"  )
	E1_1( CL_MISALIGNED_SUB_BUFFER_OFFSET,              "misaligned sub buffer offset" )
	E1_1( CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST, "exec status error for events in wait list"  )
	E1_2( CL_COMPILE_PROGRAM_FAILURE,                   "compile program failure" )      
	E1_2( CL_LINKER_NOT_AVAILABLE,                      "linker not available" )         
	E1_2( CL_LINK_PROGRAM_FAILURE,                      "link program failure" )         
	E1_2( CL_DEVICE_PARTITION_FAILED,                   "device partition failed" )      
	E1_2( CL_KERNEL_ARG_INFO_NOT_AVAILABLE,             "kernel arg info not available" )
	E1_0( CL_INVALID_VALUE,                             "invalid value"  )
	E1_0( CL_INVALID_DEVICE_TYPE,                       "invalid device type"  )
	E1_0( CL_INVALID_PLATFORM,                          "invalid platform"  )
	E1_0( CL_INVALID_DEVICE,                            "invalid device"  )
	E1_0( CL_INVALID_CONTEXT,                           "invalid context"  )
	E1_0( CL_INVALID_QUEUE_PROPERTIES,                  "invalid queue properties"  )
	E1_0( CL_INVALID_COMMAND_QUEUE,                     "invalid command queue"  )
	E1_0( CL_INVALID_HOST_PTR,                          "invalid host ptr"  )
	E1_0( CL_INVALID_MEM_OBJECT,                        "invalid mem object"  )
	E1_0( CL_INVALID_IMAGE_FORMAT_DESCRIPTOR,           "invalid image format descriptor"  )
	E1_0( CL_INVALID_IMAGE_SIZE,                        "invalid image size"  )
	E1_0( CL_INVALID_SAMPLER,                           "invalid sampler"  )
	E1_0( CL_INVALID_BINARY,                            "invalid binary"  )
	E1_0( CL_INVALID_BUILD_OPTIONS,                     "invalid build options"  )
	E1_0( CL_INVALID_PROGRAM,                           "invalid program"  )
	E1_0( CL_INVALID_PROGRAM_EXECUTABLE,                "invalid program executable"  )
	E1_0( CL_INVALID_KERNEL_NAME,                       "invalid kernel name"  )
	E1_0( CL_INVALID_KERNEL_DEFINITION,                 "invalid kernel definition"  )
	E1_0( CL_INVALID_KERNEL,                            "invalid kernel"  )
	E1_0( CL_INVALID_ARG_INDEX,                         "invalid arg index"  )
	E1_0( CL_INVALID_ARG_VALUE,                         "invalid arg value"  )
	E1_0( CL_INVALID_ARG_SIZE,                          "invalid arg size"  )
	E1_0( CL_INVALID_KERNEL_ARGS,                       "invalid kernel args"  )
	E1_0( CL_INVALID_WORK_DIMENSION,                    "invalid work dimension"  )
	E1_0( CL_INVALID_WORK_GROUP_SIZE,                   "invalid work group size"  )
	E1_0( CL_INVALID_WORK_ITEM_SIZE,                    "invalid work item size"  )
	E1_0( CL_INVALID_GLOBAL_OFFSET,                     "invalid global offset"  )
	E1_0( CL_INVALID_EVENT_WAIT_LIST,                   "invalid event wait list"  )
	E1_0( CL_INVALID_EVENT,                             "invalid event"  )
	E1_0( CL_INVALID_OPERATION,                         "invalid operation"  )
	E1_0( CL_INVALID_GL_OBJECT,                         "invalid gl object"  )
	E1_0( CL_INVALID_BUFFER_SIZE,                       "invalid buffer size"  )
	E1_0( CL_INVALID_MIP_LEVEL,                         "invalid mip level"  )
	E1_0( CL_INVALID_GLOBAL_WORK_SIZE,                  "invalid global work size"  )
	E1_2( CL_INVALID_PROPERTY,                          "invalid property"  )              
	E1_2( CL_INVALID_IMAGE_DESCRIPTOR,                  "invalid image descriptor"  )      
	E1_2( CL_INVALID_COMPILER_OPTIONS,                  "invalid compiler options"  )      
	E1_2( CL_INVALID_LINKER_OPTIONS,                    "invalid linker options"  )        
	E1_2( CL_INVALID_DEVICE_PARTITION_COUNT,            "invalid device partition count"  )
};

static const info_list_t info_list[] = 
{
	V1_0( CL_PLATFORM_PROFILE,                          "profile",                          push<char[]> )
	V1_0( CL_PLATFORM_VERSION,                          "version",                          push<char[]> )
	V1_0( CL_PLATFORM_NAME,                             "name",                             push<char[]> )
	V1_0( CL_PLATFORM_VENDOR,                           "vendor",                           push<char[]> )
	V1_0( CL_PLATFORM_EXTENSIONS,                       "extensions",                       push<char[]> )
	V1_0( CL_DEVICE_TYPE,                               "type",                             pushBitField<EBT_DEVICE_TYPE> )
	V1_0( CL_DEVICE_VENDOR_ID,                          "vendor_id",                        push<cl_uint> )
	V1_0( CL_DEVICE_MAX_COMPUTE_UNITS,                  "max_compute_units",                push<cl_uint> )
	V1_0( CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,           "max_work_item_dimensions",         push<cl_uint> )
	V1_0( CL_DEVICE_MAX_WORK_GROUP_SIZE,                "max_work_group_size",              push<size_t> )
	V1_0( CL_DEVICE_MAX_WORK_ITEM_SIZES,                "max_work_item_sizes",              pushArray<size_t> )
	V1_0( CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR,        "preferred_vector_width_char",      push<cl_uint> )
	V1_0( CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT,       "preferred_vector_width_short",     push<cl_uint> )
	V1_0( CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT,         "preferred_vector_width_int",       push<cl_uint> )
	V1_0( CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG,        "preferred_vector_width_long",      push<cl_uint> )
	V1_0( CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT,       "preferred_vector_width_float",     push<cl_uint> )
	V1_0( CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE,      "preferred_vector_width_double",    push<cl_uint> )
	V1_0( CL_DEVICE_MAX_CLOCK_FREQUENCY,                "max_clock_frequency",              push<cl_uint> )
	V1_0( CL_DEVICE_ADDRESS_BITS,                       "address_bits",                     push<cl_uint> )
	V1_0( CL_DEVICE_MAX_READ_IMAGE_ARGS,                "max_read_image_args",              push<cl_uint> )
	V1_0( CL_DEVICE_MAX_WRITE_IMAGE_ARGS,               "max_write_image_args",             push<cl_uint> )
	V1_0( CL_DEVICE_MAX_MEM_ALLOC_SIZE,                 "max_mem_alloc_size",               push<cl_ulong> )
	V1_0( CL_DEVICE_IMAGE2D_MAX_WIDTH,                  "image2d_max_width",                push<size_t> )
	V1_0( CL_DEVICE_IMAGE2D_MAX_HEIGHT,                 "image2d_max_height",               push<size_t> )
	V1_0( CL_DEVICE_IMAGE3D_MAX_WIDTH,                  "image3d_max_width",                push<size_t> )
	V1_0( CL_DEVICE_IMAGE3D_MAX_HEIGHT,                 "image3d_max_height",               push<size_t> )
	V1_0( CL_DEVICE_IMAGE3D_MAX_DEPTH,                  "image3d_max_depth",                push<size_t> )
	V1_0( CL_DEVICE_IMAGE_SUPPORT,                      "image_support",                    push<bool> )
	V1_0( CL_DEVICE_MAX_PARAMETER_SIZE,                 "max_parameter_size",               push<size_t> )
	V1_0( CL_DEVICE_MAX_SAMPLERS,                       "max_samplers",                     push<cl_uint> )
	V1_0( CL_DEVICE_MEM_BASE_ADDR_ALIGN,                "mem_base_addr_align",              push<cl_uint> )
	V1_0( CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE,           "min_data_type_align_size",         push<cl_uint> )
	V1_0( CL_DEVICE_SINGLE_FP_CONFIG,                   "single_fp_config",                 pushBitField<EBT_DEVICE_FP_CONFIG> )
	V1_0( CL_DEVICE_GLOBAL_MEM_CACHE_TYPE,              "global_mem_cache_type",            pushEnum<EBT_DEVICE_MEM_CACHE_TYPE> )
	V1_0( CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE,          "global_mem_cacheline_size",        push<cl_uint> )
	V1_0( CL_DEVICE_GLOBAL_MEM_CACHE_SIZE,              "global_mem_cache_size",            push<cl_ulong> )
	V1_0( CL_DEVICE_GLOBAL_MEM_SIZE,                    "global_mem_size",                  push<cl_ulong> )
	V1_0( CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE,           "max_constant_buffer_size",         push<cl_ulong> )
	V1_0( CL_DEVICE_MAX_CONSTANT_ARGS,                  "max_constant_args",                push<cl_uint> )
	V1_0( CL_DEVICE_LOCAL_MEM_TYPE,                     "local_mem_type",                   pushEnum<EBT_DEVICE_LOCAL_MEM_TYPE> )
	V1_0( CL_DEVICE_LOCAL_MEM_SIZE,                     "local_mem_size",                   push<cl_ulong> )
	V1_0( CL_DEVICE_ERROR_CORRECTION_SUPPORT,           "error_correction_support",         push<bool> )
	V1_0( CL_DEVICE_PROFILING_TIMER_RESOLUTION,         "profiling_timer_resolution",       push<size_t> )
	V1_0( CL_DEVICE_ENDIAN_LITTLE,                      "endian_little",                    push<bool> )
	V1_0( CL_DEVICE_AVAILABLE,                          "available",                        push<bool> )
	V1_0( CL_DEVICE_COMPILER_AVAILABLE,                 "compiler_available",               push<bool> )
	V1_0( CL_DEVICE_EXECUTION_CAPABILITIES,             "execution_capabilities",           pushBitField<EBT_DEVICE_EXEC_CAPABILITIES> )
	V1_0( CL_DEVICE_QUEUE_PROPERTIES,                   "queue_properties",                 pushBitField<EBT_COMMAND_QUEUE_PROPERTIES> )
	V1_0( CL_DEVICE_NAME,                               "name",                             push<char[]> )
	V1_0( CL_DEVICE_VENDOR,                             "vendor",                           push<char[]> )
	V1_0( CL_DRIVER_VERSION,                            "version",                          push<char[]> )
	V1_0( CL_DEVICE_PROFILE,                            "profile",                          push<char[]> )
	V1_0( CL_DEVICE_VERSION,                            "version",                          push<char[]> )
	V1_0( CL_DEVICE_EXTENSIONS,                         "extensions",                       push<char[]> )
	V1_0( CL_DEVICE_PLATFORM,                           "platform",                         push<cl_platform_id> )
	V1_2( CL_DEVICE_DOUBLE_FP_CONFIG,                   "double_fp_config",                 pushBitField<EBT_DEVICE_FP_CONFIG> )
	V1_1( CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF,        "preferred_vector_width_half",      push<cl_uint> )
	V1_1( CL_DEVICE_HOST_UNIFIED_MEMORY,                "host_unified_memory",              push<bool> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR,           "native_vector_width_char",         push<cl_uint> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT,          "native_vector_width_short",        push<cl_uint> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_INT,            "native_vector_width_int",          push<cl_uint> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG,           "native_vector_width_long",         push<cl_uint> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT,          "native_vector_width_float",        push<cl_uint> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE,         "native_vector_width_double",       push<cl_uint> )
	V1_1( CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF,           "native_vector_width_half",         push<cl_uint> )
	V1_1( CL_DEVICE_OPENCL_C_VERSION,                   "opencl_c_version",                 push<char[]> )
	V1_2( CL_DEVICE_LINKER_AVAILABLE,                   "linker_available",                 push<bool> )
	V1_2( CL_DEVICE_BUILT_IN_KERNELS,                   "built_in_kernels",                 push<char[]> )
	V1_2( CL_DEVICE_IMAGE_MAX_BUFFER_SIZE,              "image_max_buffer_size",            push<size_t> )
	V1_2( CL_DEVICE_IMAGE_MAX_ARRAY_SIZE,               "image_max_array_size",             push<size_t> )
	V1_2( CL_DEVICE_PARENT_DEVICE,                      "parent_device",                    push<cl_device_id> )
	V1_2( CL_DEVICE_PARTITION_MAX_SUB_DEVICES,          "partition_max_sub_devices",        push<cl_uint> )
	V1_2( CL_DEVICE_PARTITION_PROPERTIES,               "partition_properties",             pushEnumArray<EBT_DEVICE_PARTITION_PROPERTY> )
	V1_2( CL_DEVICE_PARTITION_AFFINITY_DOMAIN,          "partition_affinity_domain",        pushBitField<EBT_DEVICE_AFFINITY_DOMAIN> )
	V1_2( CL_DEVICE_PARTITION_TYPE,                     "partition_type",                   pushEnumArray<EBT_DEVICE_PARTITION_PROPERTY> )
	V1_2( CL_DEVICE_REFERENCE_COUNT,                    "reference_count",                  push<cl_uint> )
	V1_2( CL_DEVICE_PREFERRED_INTEROP_USER_SYNC,        "preferred_interop_user_sync",      push<bool> )
	V1_2( CL_DEVICE_PRINTF_BUFFER_SIZE,                 "printf_buffer_size",               push<size_t> )
	V1_0( CL_CONTEXT_REFERENCE_COUNT,                   "reference_count",                  push<cl_uint> )
	V1_0( CL_CONTEXT_DEVICES,                           "devices",                          pushArray<cl_device_id> )
	V1_0( CL_CONTEXT_PROPERTIES,                        "properties",                       pushArray<cl_context_properties> )
	V1_1( CL_CONTEXT_NUM_DEVICES,                       "num_devices",                      push<cl_uint> )
	V1_0( CL_QUEUE_CONTEXT,                             "context",                          push<cl_context> )
	V1_0( CL_QUEUE_DEVICE,                              "device",                           push<cl_device_id> )
	V1_0( CL_QUEUE_REFERENCE_COUNT,                     "reference_count",                  push<cl_uint> )
	V1_0( CL_QUEUE_PROPERTIES,                          "properties",                       pushBitField<EBT_COMMAND_QUEUE_PROPERTIES> )
	V1_0( CL_MEM_TYPE,                                  "type",                             pushEnum<EBT_MEM_OBJECT_TYPE> )
	V1_0( CL_MEM_FLAGS,                                 "flags",                            pushBitField<EBT_MEM_FLAGS> )
	V1_0( CL_MEM_SIZE,                                  "size",                             push<size_t> )
	V1_0( CL_MEM_HOST_PTR,                              "host_ptr",                         push<void*> )
	V1_0( CL_MEM_MAP_COUNT,                             "map_count",                        push<cl_uint> )
	V1_0( CL_MEM_REFERENCE_COUNT,                       "reference_count",                  push<cl_uint> )
	V1_0( CL_MEM_CONTEXT,                               "context",                          push<cl_context> )
	V1_1( CL_MEM_ASSOCIATED_MEMOBJECT,                  "associated_memobject",             push<cl_mem> )
	V1_1( CL_MEM_OFFSET,                                "offset",                           push<size_t> )
	V1_0( CL_IMAGE_FORMAT,                              "format",                           push<cl_image_format> )
	V1_0( CL_IMAGE_ELEMENT_SIZE,                        "element_size",                     push<size_t> )
	V1_0( CL_IMAGE_ROW_PITCH,                           "row_pitch",                        push<size_t> )
	V1_0( CL_IMAGE_SLICE_PITCH,                         "slice_pitch",                      push<size_t> )
	V1_0( CL_IMAGE_WIDTH,                               "width",                            push<size_t> )
	V1_0( CL_IMAGE_HEIGHT,                              "height",                           push<size_t> )
	V1_0( CL_IMAGE_DEPTH,                               "depth",                            push<size_t> )
	V1_2( CL_IMAGE_ARRAY_SIZE,                          "array_size",                       push<size_t> )
	V1_2( CL_IMAGE_BUFFER,                              "buffer",                           push<cl_mem> )
	V1_2( CL_IMAGE_NUM_MIP_LEVELS,                      "num_mip_levels",                   push<cl_uint> )
	V1_2( CL_IMAGE_NUM_SAMPLES,                         "num_samples",                      push<cl_uint> )
	V1_0( CL_SAMPLER_REFERENCE_COUNT,                   "reference_count",                  push<cl_uint> )
	V1_0( CL_SAMPLER_CONTEXT,                           "context",                          push<cl_context> )
	V1_0( CL_SAMPLER_NORMALIZED_COORDS,                 "normalized_coords",                push<bool> )
	V1_0( CL_SAMPLER_ADDRESSING_MODE,                   "addressing_mode",                  pushEnum<EBT_ADDRESSING_MODE> )
	V1_0( CL_SAMPLER_FILTER_MODE,                       "filter_mode",                      pushEnum<EBT_FILTER_MODE> )
	V1_0( CL_PROGRAM_REFERENCE_COUNT,                   "reference_count",                  push<cl_uint> )
	V1_0( CL_PROGRAM_CONTEXT,                           "context",                          push<cl_context> )
	V1_0( CL_PROGRAM_NUM_DEVICES,                       "num_devices",                      push<cl_uint> )
	V1_0( CL_PROGRAM_DEVICES,                           "devices",                          pushArray<cl_device_id> )
	V1_0( CL_PROGRAM_SOURCE,                            "source",                           push<char[]> )
	V1_0( CL_PROGRAM_BINARY_SIZES,                      "binary_sizes",                     pushArray<size_t> )
	V1_0( CL_PROGRAM_BINARIES,                          "binaries",                         pushBinaries )
	V1_2( CL_PROGRAM_NUM_KERNELS,                       "num_kernels",                      push<size_t> )
	V1_2( CL_PROGRAM_KERNEL_NAMES,                      "kernel_names",                     push<char[]> )
	V1_0( CL_PROGRAM_BUILD_STATUS,                      "build_status",                     pushEnum<EBT_BUILD_STATUS> )
	V1_0( CL_PROGRAM_BUILD_OPTIONS,                     "build_options",                    push<char[]> )
	V1_0( CL_PROGRAM_BUILD_LOG,                         "build_log",                        push<char[]> )
	V1_2( CL_PROGRAM_BINARY_TYPE,                       "binary_type",                      pushEnum<EBT_PROGRAM_BINARY_TYPE> )
	V1_0( CL_KERNEL_FUNCTION_NAME,                      "function_name",                    push<char[]> )
	V1_0( CL_KERNEL_NUM_ARGS,                           "num_args",                         push<cl_uint> )
	V1_0( CL_KERNEL_REFERENCE_COUNT,                    "reference_count",                  push<cl_uint> )
	V1_0( CL_KERNEL_CONTEXT,                            "context",                          push<cl_context> )
	V1_0( CL_KERNEL_PROGRAM,                            "program",                          push<cl_program> )
	V1_2( CL_KERNEL_ATTRIBUTES,                         "attributes",                       push<char[]> )
	V1_2( CL_KERNEL_ARG_ADDRESS_QUALIFIER,              "address_qualifier",                pushEnum<EBT_KERNEL_ARG_ADDRESS_QUALIFIER> )
	V1_2( CL_KERNEL_ARG_ACCESS_QUALIFIER,               "access_qualifier",                 pushEnum<EBT_KERNEL_ARG_ACCESS_QUALIFIER> )
	V1_2( CL_KERNEL_ARG_TYPE_NAME,                      "type_name",                        push<char[]> )
	V1_2( CL_KERNEL_ARG_TYPE_QUALIFIER,                 "type_qualifier",                   pushBitField<EBT_KERNEL_ARG_TYPE_QUALIFIER> )
	V1_2( CL_KERNEL_ARG_NAME,                           "name",                             push<char[]> )
	V1_0( CL_KERNEL_WORK_GROUP_SIZE,                    "work_group_size",                  push<size_t> )
	V1_0( CL_KERNEL_COMPILE_WORK_GROUP_SIZE,            "compile_work_group_size",          pushArray<size_t> )
	V1_0( CL_KERNEL_LOCAL_MEM_SIZE,                     "local_mem_size",                   push<cl_ulong> )
	V1_1( CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, "preferred_work_group_size_multiple", push<size_t> )
	V1_1( CL_KERNEL_PRIVATE_MEM_SIZE,                   "private_mem_size",                 push<cl_ulong> )
	V1_2( CL_KERNEL_GLOBAL_WORK_SIZE,                   "global_work_size",                 pushArray<size_t> )
	V1_0( CL_EVENT_COMMAND_QUEUE,                       "command_queue",                    push<cl_command_queue> )
	V1_0( CL_EVENT_COMMAND_TYPE,                        "command_type",                     push<cl_command_type> )
	V1_0( CL_EVENT_REFERENCE_COUNT,                     "reference_count",                  push<cl_uint> )
	V1_0( CL_EVENT_COMMAND_EXECUTION_STATUS,            "command_execution_status",         push<cl_int> )
	V1_1( CL_EVENT_CONTEXT,                             "context",                          push<cl_context> )
	V1_0( CL_PROFILING_COMMAND_QUEUED,                  "queued",                           push<cl_ulong> )
	V1_0( CL_PROFILING_COMMAND_SUBMIT,                  "submit",                           push<cl_ulong> )
	V1_0( CL_PROFILING_COMMAND_START,                   "start",                            push<cl_ulong> )
	V1_0( CL_PROFILING_COMMAND_END,                     "end",                              push<cl_ulong> )
	V1_0( 0xFFFF,                                       NULL,                               push<cl_uint> )
};

static const enum_list_t enum_info_list[] = 
{
	V1_0( EBT_DEVICE_TYPE,                  CL_DEVICE_TYPE_DEFAULT,                       "default" )
	V1_0( EBT_DEVICE_TYPE,                  CL_DEVICE_TYPE_CPU,                           "cpu" )
	V1_0( EBT_DEVICE_TYPE,                  CL_DEVICE_TYPE_GPU,                           "gpu" )
	V1_0( EBT_DEVICE_TYPE,                  CL_DEVICE_TYPE_ACCELERATOR,                   "accelerator" )
	V1_2( EBT_DEVICE_TYPE,                  CL_DEVICE_TYPE_CUSTOM,                        "custom" )
	V1_0( EBT_DEVICE_TYPE,                  CL_DEVICE_TYPE_ALL,                           "all" )
	V1_0( EBT_DEVICE_FP_CONFIG,             CL_FP_DENORM,                                 "denorm" )
	V1_0( EBT_DEVICE_FP_CONFIG,             CL_FP_INF_NAN,                                "inf_nan" )
	V1_0( EBT_DEVICE_FP_CONFIG,             CL_FP_ROUND_TO_NEAREST,                       "round_to_nearest" )
	V1_0( EBT_DEVICE_FP_CONFIG,             CL_FP_ROUND_TO_ZERO,                          "round_to_zero" )
	V1_0( EBT_DEVICE_FP_CONFIG,             CL_FP_ROUND_TO_INF,                           "round_to_inf" )
	V1_0( EBT_DEVICE_FP_CONFIG,             CL_FP_FMA,                                    "fma" )
	V1_1( EBT_DEVICE_FP_CONFIG,             CL_FP_SOFT_FLOAT,                             "soft_float" )
	V1_2( EBT_DEVICE_FP_CONFIG,             CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT,          "correctly_rounded_divide_sqrt" )
	V1_0( EBT_DEVICE_MEM_CACHE_TYPE,        CL_NONE,                                      "none" )
	V1_0( EBT_DEVICE_MEM_CACHE_TYPE,        CL_READ_ONLY_CACHE,                           "readonly_cache" )
	V1_0( EBT_DEVICE_MEM_CACHE_TYPE,        CL_READ_WRITE_CACHE,                          "readwrite_cache" )
	V1_0( EBT_DEVICE_LOCAL_MEM_TYPE,        CL_LOCAL,                                     "local" )
	V1_0( EBT_DEVICE_LOCAL_MEM_TYPE,        CL_GLOBAL,                                    "global" )
	V1_0( EBT_DEVICE_EXEC_CAPABILITIES,     CL_EXEC_KERNEL,                               "kernel" )
	V1_0( EBT_DEVICE_EXEC_CAPABILITIES,     CL_EXEC_NATIVE_KERNEL,                        "native_kernel" )
	V1_0( EBT_COMMAND_QUEUE_PROPERTIES,     CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE,       "out_of_order_exec_mode_enable" )
	V1_0( EBT_COMMAND_QUEUE_PROPERTIES,     CL_QUEUE_PROFILING_ENABLE,                    "profiling_enable" )
	V1_0( EBT_CONTEXT_PROPERTIES,           CL_CONTEXT_PLATFORM,                          "platform" )
	V1_2( EBT_CONTEXT_PROPERTIES,           CL_CONTEXT_INTEROP_USER_SYNC,                 "interop_user_sync" )
	V1_2( EBT_DEVICE_PARTITION_PROPERTY,    CL_DEVICE_PARTITION_EQUALLY,                  "equally" )
	V1_2( EBT_DEVICE_PARTITION_PROPERTY,    CL_DEVICE_PARTITION_BY_COUNTS,                "by_counts" )
	V1_2( EBT_DEVICE_PARTITION_PROPERTY,    CL_DEVICE_PARTITION_BY_COUNTS_LIST_END,       "by_counts_list_end" )
	V1_2( EBT_DEVICE_PARTITION_PROPERTY,    CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN,       "by_affinity_domain" )
	V1_2( EBT_DEVICE_AFFINITY_DOMAIN,       CL_DEVICE_AFFINITY_DOMAIN_NUMA,               "numa" )
	V1_2( EBT_DEVICE_AFFINITY_DOMAIN,       CL_DEVICE_AFFINITY_DOMAIN_L4_CACHE,           "l4_cache" )
	V1_2( EBT_DEVICE_AFFINITY_DOMAIN,       CL_DEVICE_AFFINITY_DOMAIN_L3_CACHE,           "l3_cache" )
	V1_2( EBT_DEVICE_AFFINITY_DOMAIN,       CL_DEVICE_AFFINITY_DOMAIN_L2_CACHE,           "l2_cache" )
	V1_2( EBT_DEVICE_AFFINITY_DOMAIN,       CL_DEVICE_AFFINITY_DOMAIN_L1_CACHE,           "l1_cache" )
	V1_2( EBT_DEVICE_AFFINITY_DOMAIN,       CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE, "next_partitionable" )
	V1_0( EBT_MEM_FLAGS,                    CL_MEM_READ_WRITE,                            "read_write" )
	V1_0( EBT_MEM_FLAGS,                    CL_MEM_WRITE_ONLY,                            "write_only" )
	V1_0( EBT_MEM_FLAGS,                    CL_MEM_READ_ONLY,                             "read_only" )
	V1_0( EBT_MEM_FLAGS,                    CL_MEM_USE_HOST_PTR,                          "use_host_ptr" )
	V1_0( EBT_MEM_FLAGS,                    CL_MEM_ALLOC_HOST_PTR,                        "alloc_host_ptr" )
	V1_0( EBT_MEM_FLAGS,                    CL_MEM_COPY_HOST_PTR,                         "copy_host_ptr" )
	V1_2( EBT_MEM_FLAGS,                    CL_MEM_HOST_WRITE_ONLY,                       "host_write_only" )
	V1_2( EBT_MEM_FLAGS,                    CL_MEM_HOST_READ_ONLY,                        "host_read_only" )
	V1_2( EBT_MEM_FLAGS,                    CL_MEM_HOST_NO_ACCESS,                        "host_no_access" )
	V1_2( EBT_MEM_MIGRATION_FLAGS,          CL_MIGRATE_MEM_OBJECT_HOST,                   "host" )
	V1_2( EBT_MEM_MIGRATION_FLAGS,          CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED,      "content_undefined" )
	V1_0( EBT_CHANNEL_ORDER,                CL_R,                                         "r" )
	V1_0( EBT_CHANNEL_ORDER,                CL_A,                                         "a" )
	V1_0( EBT_CHANNEL_ORDER,                CL_RG,                                        "rg" )
	V1_0( EBT_CHANNEL_ORDER,                CL_RA,                                        "ra" )
	V1_0( EBT_CHANNEL_ORDER,                CL_RGB,                                       "rgb" )
	V1_0( EBT_CHANNEL_ORDER,                CL_RGBA,                                      "rgba" )
	V1_0( EBT_CHANNEL_ORDER,                CL_BGRA,                                      "bgra" )
	V1_0( EBT_CHANNEL_ORDER,                CL_ARGB,                                      "argb" )
	V1_0( EBT_CHANNEL_ORDER,                CL_INTENSITY,                                 "intensity" )
	V1_0( EBT_CHANNEL_ORDER,                CL_LUMINANCE,                                 "luminance" )
	V1_1( EBT_CHANNEL_ORDER,                CL_Rx,                                        "rx" )
	V1_1( EBT_CHANNEL_ORDER,                CL_RGx,                                       "rgx" )
	V1_1( EBT_CHANNEL_ORDER,                CL_RGBx,                                      "rgbx" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_SNORM_INT8,                                "snormint8" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_SNORM_INT16,                               "snormint16" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNORM_INT8,                                "unormint8" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNORM_INT16,                               "unormint16" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNORM_SHORT_565,                           "unormshort_565" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNORM_SHORT_555,                           "unormshort_555" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNORM_INT_101010,                          "unormint_101010" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_SIGNED_INT8,                               "signedint8" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_SIGNED_INT16,                              "signedint16" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_SIGNED_INT32,                              "signedint32" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNSIGNED_INT8,                             "unsignedint8" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNSIGNED_INT16,                            "unsignedint16" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_UNSIGNED_INT32,                            "unsignedint32" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_HALF_FLOAT,                                "halffloat" )
	V1_0( EBT_CHANNEL_TYPE,                 CL_FLOAT,                                     "float" )
	V1_0( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_BUFFER,                         "buffer" )
	V1_0( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_IMAGE2D,                        "image2d" )
	V1_0( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_IMAGE3D,                        "image3d" )
	V1_2( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_IMAGE2D_ARRAY,                  "image2d_array" )
	V1_2( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_IMAGE1D,                        "image1d" )
	V1_2( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_IMAGE1D_ARRAY,                  "image1d_array" )
	V1_2( EBT_MEM_OBJECT_TYPE,              CL_MEM_OBJECT_IMAGE1D_BUFFER,                 "image1d_buffer" )
	V1_0( EBT_ADDRESSING_MODE,              CL_ADDRESS_NONE,                              "none" )
	V1_0( EBT_ADDRESSING_MODE,              CL_ADDRESS_CLAMP_TO_EDGE,                     "clamp_to_edge" )
	V1_0( EBT_ADDRESSING_MODE,              CL_ADDRESS_CLAMP,                             "clamp" )
	V1_0( EBT_ADDRESSING_MODE,              CL_ADDRESS_REPEAT,                            "repeat" )
	V1_1( EBT_ADDRESSING_MODE,              CL_ADDRESS_MIRRORED_REPEAT,                   "mirrored_repeat" )
	V1_0( EBT_FILTER_MODE,                  CL_FILTER_NEAREST,                            "nearest" )
	V1_0( EBT_FILTER_MODE,                  CL_FILTER_LINEAR,                             "linear" )
	V1_0( EBT_MAP_FLAGS,                    CL_MAP_READ,                                  "read" )
	V1_0( EBT_MAP_FLAGS,                    CL_MAP_WRITE,                                 "write" )
	V1_2( EBT_MAP_FLAGS,                    CL_MAP_WRITE_INVALIDATE_REGION,               "write_invalidate_region" )
	V1_2( EBT_PROGRAM_BINARY_TYPE,          CL_PROGRAM_BINARY_TYPE_NONE,                  "none" )
	V1_2( EBT_PROGRAM_BINARY_TYPE,          CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT,       "compiled_object" )
	V1_2( EBT_PROGRAM_BINARY_TYPE,          CL_PROGRAM_BINARY_TYPE_LIBRARY,               "library" )
	V1_2( EBT_PROGRAM_BINARY_TYPE,          CL_PROGRAM_BINARY_TYPE_EXECUTABLE,            "executable" )
	V1_0( EBT_BUILD_STATUS,                 CL_BUILD_SUCCESS,                             "success" )
	V1_0( EBT_BUILD_STATUS,                 CL_BUILD_NONE,                                "none" )
	V1_0( EBT_BUILD_STATUS,                 CL_BUILD_ERROR,                               "error" )
	V1_0( EBT_BUILD_STATUS,                 CL_BUILD_IN_PROGRESS,                         "in_progress" )
	V1_2( EBT_KERNEL_ARG_ADDRESS_QUALIFIER, CL_KERNEL_ARG_ADDRESS_GLOBAL,                 "global" )
	V1_2( EBT_KERNEL_ARG_ADDRESS_QUALIFIER, CL_KERNEL_ARG_ADDRESS_LOCAL,                  "local" )
	V1_2( EBT_KERNEL_ARG_ADDRESS_QUALIFIER, CL_KERNEL_ARG_ADDRESS_CONSTANT,               "constant" )
	V1_2( EBT_KERNEL_ARG_ADDRESS_QUALIFIER, CL_KERNEL_ARG_ADDRESS_PRIVATE,                "private" )
	V1_2( EBT_KERNEL_ARG_ACCESS_QUALIFIER,  CL_KERNEL_ARG_ACCESS_READ_ONLY,               "read_only" )
	V1_2( EBT_KERNEL_ARG_ACCESS_QUALIFIER,  CL_KERNEL_ARG_ACCESS_WRITE_ONLY,              "write_only" )
	V1_2( EBT_KERNEL_ARG_ACCESS_QUALIFIER,  CL_KERNEL_ARG_ACCESS_READ_WRITE,              "read_write" )
	V1_2( EBT_KERNEL_ARG_ACCESS_QUALIFIER,  CL_KERNEL_ARG_ACCESS_NONE,                    "none" )
	V1_2( EBT_KERNEL_ARG_TYPE_QUALIFIER,    CL_KERNEL_ARG_TYPE_NONE,                      "type_none" )
	V1_2( EBT_KERNEL_ARG_TYPE_QUALIFIER,    CL_KERNEL_ARG_TYPE_CONST,                     "type_const" )
	V1_2( EBT_KERNEL_ARG_TYPE_QUALIFIER,    CL_KERNEL_ARG_TYPE_RESTRICT,                  "type_restrict" )
	V1_2( EBT_KERNEL_ARG_TYPE_QUALIFIER,    CL_KERNEL_ARG_TYPE_VOLATILE,                  "type_volatile" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_NDRANGE_KERNEL,                    "ndrange_kernel" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_TASK,                              "task" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_NATIVE_KERNEL,                     "native_kernel" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_READ_BUFFER,                       "read_buffer" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_WRITE_BUFFER,                      "write_buffer" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_COPY_BUFFER,                       "copy_buffer" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_READ_IMAGE,                        "read_image" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_WRITE_IMAGE,                       "write_image" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_COPY_IMAGE,                        "copy_image" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_COPY_IMAGE_TO_BUFFER,              "copy_image_to_buffer" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_COPY_BUFFER_TO_IMAGE,              "copy_buffer_to_image" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_MAP_BUFFER,                        "map_buffer" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_MAP_IMAGE,                         "map_image" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_UNMAP_MEM_OBJECT,                  "unmap_mem_object" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_MARKER,                            "marker" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_ACQUIRE_GL_OBJECTS,                "acquire_gl_objects" )
	V1_0( EBT_COMMAND_TYPE,                 CL_COMMAND_RELEASE_GL_OBJECTS,                "release_gl_objects" )
	V1_1( EBT_COMMAND_TYPE,                 CL_COMMAND_READ_BUFFER_RECT,                  "read_buffer_rect" )
	V1_1( EBT_COMMAND_TYPE,                 CL_COMMAND_WRITE_BUFFER_RECT,                 "write_buffer_rect" )
	V1_1( EBT_COMMAND_TYPE,                 CL_COMMAND_COPY_BUFFER_RECT,                  "copy_buffer_rect" )
	V1_1( EBT_COMMAND_TYPE,                 CL_COMMAND_USER,                              "user" )
	V1_2( EBT_COMMAND_TYPE,                 CL_COMMAND_BARRIER,                           "barrier" )              
	V1_2( EBT_COMMAND_TYPE,                 CL_COMMAND_MIGRATE_MEM_OBJECTS,               "migrate_mem_objects" )  
	V1_2( EBT_COMMAND_TYPE,                 CL_COMMAND_FILL_BUFFER,                       "fill_buffer" )          
	V1_2( EBT_COMMAND_TYPE,                 CL_COMMAND_FILL_IMAGE,                        "fill_image" )           
	V1_0( EBT_COMMAND_EXECUTION_STATUS,     CL_COMPLETE,                                  "complete" )
	V1_0( EBT_COMMAND_EXECUTION_STATUS,     CL_RUNNING,                                   "running" )
	V1_0( EBT_COMMAND_EXECUTION_STATUS,     CL_SUBMITTED,                                 "submitted" )
	V1_0( EBT_COMMAND_EXECUTION_STATUS,     CL_QUEUED,                                    "queued" )
	V1_1( EBT_BUFFER_CREATE_TYPE,           CL_BUFFER_CREATE_TYPE_REGION,                 "region" )
};

static const cl_ushort first_info_ids[IT_MAX+1] = {
	CL_PLATFORM_PROFILE, CL_DEVICE_TYPE, CL_CONTEXT_REFERENCE_COUNT, CL_QUEUE_CONTEXT, 
	CL_MEM_TYPE, CL_IMAGE_FORMAT, CL_SAMPLER_REFERENCE_COUNT, CL_PROGRAM_REFERENCE_COUNT,
	CL_PROGRAM_BUILD_STATUS, CL_KERNEL_FUNCTION_NAME, CL_KERNEL_WORK_GROUP_SIZE,
	CL_EVENT_COMMAND_QUEUE, CL_PROFILING_COMMAND_QUEUED, 0xFFFF
};

#define countof(a) (sizeof(a)/sizeof(a[0]))
// Some functions for handling those tables
static int getInfoTable(const info_list_t*& pinfo, eInfoTable info_table)
{
	int i, j, fid = first_info_ids[info_table];
	for(i=0;info_list[i].id<fid;i++) ;
	pinfo = info_list + i;
	fid = first_info_ids[info_table+1];
	for(j=i;info_list[j].id<fid;j++) ;
	return j-i;
}

static int getEnumTable(const enum_list_t*& ptable, enumTypes enum_type)
{
	size_t i, j;
	for(i=0;i<countof(enum_info_list);i++)
		if(enum_info_list[i].type_id == enum_type)
			break;
	ptable = enum_info_list + i;
	for(j=i;i<countof(enum_info_list);j++)
		if(enum_info_list[j].type_id != enum_type)
			break;
	return (int)(j-i);
}

static void error_check(lua_State* L, int error_code)
{
	if(error_code == CL_SUCCESS)
		return;
	const char* str = "unknown error";
	for(size_t i=0;i<countof(error_info_list);i++)
	{
		if(error_info_list[i].id == error_code)
		{
			str = error_info_list[i].name;
			break;
		}
	}
	luaL_error(L, "OpenCL: %s", str);
}
