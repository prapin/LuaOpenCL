#define LUA_LIB
#define lua_c
#include "lua.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

static lua_State *globalL = NULL;

#if defined(LUA_USE_DYNAMIC_READLINE)
#include <windows.h>
/* The structure used to store a history entry. */
typedef struct _hist_entry {
  char* line;
  char* timestamp;		/* char * rather than time_t for read/write */
  void* data;
} HIST_ENTRY;
 
typedef char* rl_compentry_func_t (const char *, int);
typedef char** rl_completion_func_t (const char *, int, int);
typedef int rl_command_func_t (int, int); 
static char* (*readline) (const char *);
static void (*add_history) (const char *);
static char** (*rl_completion_matches) (const char *, rl_compentry_func_t *);
static void (*xfree)(void*) = free;
static void* (*xmalloc)(size_t) = malloc;
static const char** (*rl_funmap_names)(void);
static HIST_ENTRY** (*history_list)(void);
static int (*read_history)(const char *filename); 
static int (*write_history)(const char *filename); 
static void (*clear_history)(void);
static void (*stifle_history)(int max);
static int (*unstifle_history)(void);
static int (*append_history)(int nelements, const char *filename);
static int (*read_history_range)(const char *filename, int from, int to);
static rl_command_func_t* (*rl_named_function)(const char *name);
static int (*rl_read_init_file)(const char *filename);
static void (*rl_variable_dumper)(int readable);
static int (*rl_variable_bind)(const char *variable, const char *value);
static void (*rl_macro_dumper)(int readable);
static void (*rl_function_dumper )(int readable);
static char** (*rl_invoking_keyseqs)(rl_command_func_t *function);
static int (*history_expand) (char *string, char **output);

static FILE* (*fopen2)( const char *filename, const char *mode );
static void (*fclose2)(FILE* file);
static int (*fprintf2)( FILE *stream, const char *format, ...);

static char** prl_line_buffer;
static rl_completion_func_t** prl_attempted_completion_function;
static char** prl_basic_word_break_characters;
static int* prl_completion_append_character;
static char** prl_readline_name;
static FILE** prl_outstream;

#define rl_line_buffer *prl_line_buffer
#define rl_attempted_completion_function *prl_attempted_completion_function
#define rl_basic_word_break_characters *prl_basic_word_break_characters
#define rl_completion_append_character *prl_completion_append_character
#define rl_readline_name *prl_readline_name
#define rl_outstream *prl_outstream

typedef struct 
{
	const char* Name;
	const void* pVariable;
} tImportedFct;
static tImportedFct ImportedFct[] = 
{
	{ "xmalloc", &xmalloc },
	{ "xfree", &xfree },
	{ "readline", &readline },
	{ "add_history", &add_history },
	{ "rl_completion_matches", &rl_completion_matches },
	{ "rl_funmap_names", &rl_funmap_names },
	{ "history_list", &history_list },
	{ "read_history", &read_history },
	{ "write_history", &write_history },
	{ "clear_history", &clear_history },
	{ "stifle_history", &stifle_history },
	{ "unstifle_history", &unstifle_history },
	{ "append_history", &append_history },
	{ "read_history_range", &read_history_range },
	{ "rl_named_function", &rl_named_function },
	{ "rl_read_init_file", &rl_read_init_file },
	{ "rl_variable_dumper", &rl_variable_dumper },
	{ "rl_macro_dumper", &rl_macro_dumper },
	{ "rl_function_dumper", &rl_function_dumper  },
	{ "rl_invoking_keyseqs", &rl_invoking_keyseqs  },
	{ "history_expand", &history_expand  },
	{ "rl_variable_bind", &rl_variable_bind  },

	{ "rl_line_buffer", &prl_line_buffer },
	{ "rl_attempted_completion_function", &prl_attempted_completion_function },
	{ "rl_basic_word_break_characters", &prl_basic_word_break_characters },
	{ "rl_completion_append_character", &prl_completion_append_character },
	{ "rl_readline_name", &prl_readline_name },
	{ "rl_outstream", &prl_outstream },
	{ NULL, NULL },
};
static tImportedFct ImportedFctMSVCRT[] = 
{
	{ "fopen", &fopen2 },
	{ "fclose", &fclose2 },
	{ "fprintf", &fprintf2 },
	{ NULL, NULL },
};
static int load_function_table(const char* dllname, tImportedFct* fct)
{
	int i = 0;
	HMODULE module = LoadLibraryA(dllname);
	if(module == NULL)
		return 0;
	if(GetProcAddress(module, "xmalloc") == NULL)
		i = 2;
	for(;fct[i].pVariable;i++)
	{
		FARPROC proc = GetProcAddress(module, fct[i].Name);
		if(proc == NULL)
			return 0;
		*(FARPROC*)fct[i].pVariable = proc;
	}
	return 1;
}
#else
#define fopen2 fopen
#define fclose2 fclose
#define fprintf2 fprintf
#endif

static int lua_readline(struct lua_State* L, char** buffer, const char* prompt)
{
	int res;
	char* expbuffer;
	L=L;
	*buffer = readline(prompt);
	if(*buffer == NULL)
		return 0;
	expbuffer = NULL;
	res = history_expand(*buffer, &expbuffer);
	if(res > 0)
	{
		xfree(*buffer);
		*buffer = expbuffer;
		printf("%s\n", expbuffer);
	}
	else
		xfree(expbuffer);
	return 1;
}

static void lua_saveline(struct lua_State* L, int idx)
{
	L=L;
	if (lua_rawlen(L,idx) > 0)  /* non-empty line? */
	  add_history(lua_tostring(L, idx));  /* add it to history */
}

static void lua_freeline(struct lua_State* L, char* buffer)
{
	L=L;
	xfree(buffer);
}

static char* table_iterator (const char* text, int state)
{
	size_t len;
	const char* str;
	char* result;
	lua_State* L = globalL;
	text=text;
	lua_rawgeti(L, -1, state+1);
	if(lua_isnil(L, -1))
		return NULL;
	str = lua_tolstring(L, -1, &len);
	result = (char*)xmalloc(len + 1);
	strcpy(result, str);
	lua_pop(L, 1);
	return result;
}

static char** do_completion (const char* text, int istart, int iend)
{
	char** matches = NULL;
	lua_State* L = globalL;
	int top;
	if(L == NULL)
		return NULL;
	top = lua_gettop(L);
	lua_getglobal(L, "utils");
	if(!lua_istable(L, -1))
		goto end;
	lua_getfield(L, -1, "completion");
	if(!lua_isfunction(L, -1))
		goto end;
	lua_pushstring(L, text);
	lua_pushstring(L, rl_line_buffer);
	lua_pushinteger(L, istart+1);
	lua_pushinteger(L, iend+1);
	if(lua_pcall(L, 4, 1, 0))
	{
#ifdef _DEBUG
		printf("\n%s\n", lua_tostring(L, -1));
#endif
		goto end;
	}
	if(!lua_istable(L, -1))
	{
#ifdef _DEBUG
		printf("\nCompletion function did not return a table\n");
#endif
		goto end;
	}
    rl_completion_append_character = 0;
    matches = rl_completion_matches (text, table_iterator);
end:
	lua_settop(L, top);
	return (matches);
}
static int check_result(lua_State* L, int result)
{
	if(result)
		luaL_error(L, strerror(errno));
	return 0;
}

static int hist_get(lua_State* L)
{
	int i;
	HIST_ENTRY ** histo = history_list();
	lua_createtable(L, 0, 0);
	for(i=0;histo[i];i++)
	{
		lua_pushstring(L, histo[i]->line);
		lua_rawseti(L, -2, i+1);
	}
	return 1;
}
static int hist_set(lua_State* L)
{
	int i, len;
	luaL_checktype(L, 1, LUA_TTABLE);
	clear_history();
	len = (int)lua_rawlen(L, 1);
	for(i=0;i<len;i++)
	{
		lua_rawgeti(L, 1, i+1);
		add_history (lua_tostring(L, -1));
		lua_settop(L, 1);
	}
	return 0;
}
static int hist_read(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	int from = (int)luaL_optinteger(L, 2, 0);
	int to = (int)luaL_optinteger(L, 3, 0);
	if(from > 0)
		return check_result(L, read_history_range(name, from-1, to-1));
	else
		return check_result(L, read_history(name));
}
static int hist_write(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	int n = (int)luaL_optinteger(L, 2, 0);
	if(n>0)
		return check_result(L, append_history(n-1, name));
	else
		return check_result(L, write_history(name));
}
static int hist_print(lua_State* L)
{
	int i;
	HIST_ENTRY ** histo = history_list();
	lua_getglobal(L, "print");
	for(i=0;histo[i];i++)
	{
		lua_pushvalue(L, -1);
		lua_pushfstring(L, "%d: %s", i+1, histo[i]->line);
		lua_pcall(L, 1, 0, 0);
	}
	return 0;
}
static int hist_clear(lua_State* L)
{
	L=L;
	clear_history();
	return 0;
}
static int hist_stifle(lua_State* L)
{
	int n = (int)luaL_optinteger(L, 1, -1);
	if(n >= 0)
	{
		lua_pushinteger(L, n);
		stifle_history(n);
	}
	else
	{
		lua_pushinteger(L, unstifle_history());
	}
	return 1;
}
static int readline_listfct(lua_State* L)
{
	int i;
	const char** pstr = rl_funmap_names();
	lua_createtable(L, 0, 0);
	for(i=0;pstr[i];i++)
	{
		lua_pushstring(L, pstr[i]);
		lua_rawseti(L, -2, i+1);
	}
	xfree((void*)pstr);
	return 1;
}
static int readline_call(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	int key = (int)luaL_optinteger(L, 2, 0);
	int value = (int)luaL_optinteger(L, 3, 0);
	rl_command_func_t* fct = rl_named_function(name);
	if(fct == NULL)
		return 0;
	fct(key, value);
	return 1;
}
static int readline_read(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	return check_result(L, rl_read_init_file(name));
}
static int readline_write(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	FILE* file = fopen2(name, "wt");
	FILE* savefile = rl_outstream;
	rl_outstream = file;
	fprintf2(file, "# Dumping variables:\n");
	rl_variable_dumper(1);
	fprintf2(file, "\n# Dumping macros:\n");
	rl_macro_dumper(1);
	fprintf2(file, "\n# Dumping function binding:\n");
	rl_function_dumper(1);
	fclose2(file);
	rl_outstream = savefile;
	return 0;
}
static int readline_get(lua_State* L)
{
	int i, j;
	const char *filename;
	const char **pstr;
	FILE *file, *savefile;
	char varname[80], value[100];
	lua_getglobal(L, "os");
	lua_getfield(L, -1, "tmpname");
	lua_pcall(L, 0, 1, 0);
	filename = lua_tostring(L, -1);
	lua_createtable(L, 0, 3);

	pstr = rl_funmap_names();
	lua_createtable(L, 0, 0);
	for(i=0;pstr[i];i++)
	{
		char** keys;
		rl_command_func_t* fct = rl_named_function(pstr[i]);
		if(fct == NULL)
			continue;
		keys = rl_invoking_keyseqs(fct);
		luaL_gsub(L, pstr[i], "-", "_");
		lua_createtable(L, 0, 0);
		for(j=0;keys && keys[j];j++)
		{
			lua_pushstring(L, keys[j]);
			lua_rawseti(L, -2, j+1);
		}
		lua_settable(L, -3);
	}
	xfree((void*)pstr);
	lua_setfield(L, -2, "Functions");

	file = fopen2(filename, "wt");
	savefile = rl_outstream;
	rl_outstream = file;
	rl_variable_dumper(1);
	fclose2(file);
	file = fopen(filename, "rt");
	lua_createtable(L, 0, 0);
	while(fscanf(file, "set %s %s\n", varname, value) != EOF)
	{
		luaL_gsub(L, varname, "-", "_");
		if(strcmp(value, "off") == 0)
			lua_pushboolean(L, 0);
		else if(strcmp(value, "on") == 0)
			lua_pushboolean(L, 1);
		else 
			lua_pushstring(L, value);
		lua_settable(L, -3);
	}
	fclose(file);
	lua_setfield(L, -2, "Variables");

	file = fopen2(filename, "wt");
	rl_macro_dumper(0);
	fclose2(file);
	file = fopen(filename, "rt");
	lua_createtable(L, 0, 0);
	i = 1;
	while(fscanf(file, "%s outputs %s\n", varname, value) != EOF)
	{
		lua_createtable(L, 0, 0);
		lua_pushstring(L, varname);
		lua_setfield(L, -2, "Keys");
		lua_pushstring(L, value);
		lua_setfield(L, -2, "Output");
		lua_rawseti(L, -2, i++);
	}
	fclose(file);
	lua_setfield(L, -2, "Macros");

	remove(filename);
	rl_outstream = savefile;
	return 1;
}
static int readline_set(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_settop(L, 1);
	lua_getfield(L, 1, "Functions");
	if(lua_istable(L, 2))
	{
		lua_pushnil(L);
		while(lua_next(L, 2))
		{
			const char* key;
			luaL_gsub(L, lua_tostring(L, -2), "_", "-");
			key = lua_tostring(L, -1);
			lua_pop(L, 2);
		}
	}
	lua_settop(L, 1);
	return 0;
}
static int readline_print(lua_State* L)
{
	L=L;
	printf("Dumping function bindings:\n");
	rl_function_dumper(0);
	printf("\n\nDumping variables:\n");
	rl_variable_dumper(0);
	printf("\n\nDumping macros:\n");
	rl_macro_dumper(0);
	return 0;
}
static const luaL_Reg historylib[] = {
	{ "read",  hist_read },
	{ "write", hist_write },
	{ "clear", hist_clear },
	{ "stifle", hist_stifle },
	{ "get",   hist_get },
	{ "set",   hist_set },
	{ "print", hist_print },
	{ NULL, NULL },
};


/* Below is a copy of the interpreter from lua.c */
static void lstop (lua_State *L, lua_Debug *ar) {
  (void)ar;  /* unused arg. */
  lua_sethook(L, NULL, 0, 0);
  luaL_error(L, "interrupted!");
}

static void laction (int i) {
  signal(i, SIG_DFL); /* if another SIGINT happens before lstop,
                              terminate process (default action) */
  lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}

static void l_message (const char *pname, const char *msg) {
  if (pname) fprintf(stderr, "%s: ", pname);
  fprintf(stderr, "%s\n", msg);
  fflush(stderr);
}


static int report (lua_State *L, int status) {
  if (status && !lua_isnil(L, -1)) {
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "(error object is not a string)";
    l_message("Lua", msg);
    lua_pop(L, 1);
  }
  return status;
}

static const char *get_prompt (lua_State *L, int firstline) {
  const char *p;
  lua_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2");
  p = lua_tostring(L, -1);
  if (p == NULL) p = (firstline ? LUA_PROMPT : LUA_PROMPT2);
  lua_pop(L, 1);  /* remove global */
  return p;
}


static int incomplete (lua_State *L, int status) {
  if (status == LUA_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = lua_tolstring(L, -1, &lmsg);
    const char *tp = msg + lmsg - (sizeof(LUA_QL("<eof>")) - 1);
    if (strstr(msg, LUA_QL("<eof>")) == tp) {
      lua_pop(L, 1);
      return 1;
    }
  }
  return 0;  /* else... */
}

static int pushline (lua_State *L, int firstline) {
  char buffer[LUA_MAXINPUT];
  char *b = buffer;
  size_t l;
  const char *prmt;

  prmt = get_prompt(L, firstline);
  if (lua_readline(L, &b, prmt) == 0)
    return 0;  /* no input */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[l-1] = '\0';  /* remove it */
  if (firstline && b[0] == '=')  /* first line starts with `=' ? */
    lua_pushfstring(L, "return %s", b+1);  /* change it to `return' */
  else
    lua_pushstring(L, b);
  lua_freeline(L, b);
  return 1;
}

static int loadline (lua_State *L) {
  int status;
  lua_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  for (;;) {  /* repeat until gets a complete line */
    size_t l;
    const char *line = lua_tolstring(L, 1, &l);
    status = luaL_loadbuffer(L, line, l, "=stdin");
    if (!incomplete(L, status)) break;  /* cannot try to add lines? */
    if (!pushline(L, 0))  /* no more input? */
      return -1;
    lua_pushliteral(L, "\n");  /* add a new line... */
    lua_insert(L, -2);  /* ...between the two lines */
    lua_concat(L, 3);  /* join them */
  }
  lua_saveline(L, 1);
  lua_remove(L, 1);  /* remove line */
  return status;
}

static int traceback (lua_State *L) {
  lua_getglobal(L, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}

static int docall (lua_State *L, int narg, int clear) {
  int status;
  int base = lua_gettop(L) - narg;  /* function index */
  lua_pushcfunction(L, traceback);  /* push traceback function */
  lua_insert(L, base);  /* put it under chunk and args */
  signal(SIGINT, laction);
  status = lua_pcall(L, narg, (clear ? 0 : LUA_MULTRET), base);
  signal(SIGINT, SIG_DFL);
  lua_remove(L, base);  /* remove traceback function */
  /* force a complete garbage collection in case of errors */
  if (status != 0) lua_gc(L, LUA_GCCOLLECT, 0);
  return status;
}

static int readline_cmd (lua_State *L) {
  int status;
  while ((status = loadline(L)) != -1) {
    if (status == 0) status = docall(L, 0, 0);
    report(L, status);
    if (status == 0 && lua_gettop(L) > 0) {  /* any result to print? */
      lua_getglobal(L, "print");
      lua_insert(L, 1);
      if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
        l_message(NULL, lua_pushfstring(L,
                               "error calling " LUA_QL("print") " (%s)",
                               lua_tostring(L, -1)));
    }
  }
  lua_settop(L, 0);  /* clear stack */
  fputs("\n", stdout);
  fflush(stdout);
  return 0;
}


static const luaL_Reg readlinelib[] = {
	{ "read",  readline_read },
	{ "write",  readline_write },
	{ "listfct", readline_listfct },
	{ "call", readline_call },
	{ "get", readline_get },
	{ "set", readline_set },
	{ "print", readline_print },
	{ "cmd", readline_cmd },
	{ NULL, NULL },
};

#if defined(LUA_USE_DYNAMIC_READLINE) || defined(LUA_USE_READLINE)
static int luaopen_history (lua_State *L) {
	luaL_newlib (L, historylib);
	return 1;
}
static int luaopen_lreadline2(lua_State *L) {
	luaL_newlib (L, readlinelib);
	return 1;
}
#endif

int luaopen_lreadline(lua_State* L)
{
#if defined(LUA_USE_DYNAMIC_READLINE)
	if(!load_function_table("readline5.dll", ImportedFct))
		if(!load_function_table("readline6.dll", ImportedFct))
			luaL_error(L, "Cannot load readline.dll");
	if(!load_function_table("msvcrt.dll", ImportedFctMSVCRT))
		luaL_error(L, "Cannot load msvcrt.dll");
#endif
	globalL = L;
	rl_attempted_completion_function = do_completion;
	rl_basic_word_break_characters = " \t\n\"\\'><=;:+-*/%^~#{}()[].,";
	rl_readline_name = "luadura";
	rl_variable_bind("bell-style", "none"); // Temporary ??
	luaL_requiref(L, "history", luaopen_history, 1);
	luaL_requiref(L, "lreadline", luaopen_lreadline2, 1);
	lua_createtable(L, 0, 4);
	lua_pushlightuserdata(L, lua_readline);
	lua_setfield(L, -2, "readline");
	lua_pushlightuserdata(L, lua_saveline);
	lua_setfield(L, -2, "saveline");
	lua_pushlightuserdata(L, lua_freeline);
	lua_setfield(L, -2, "freeline");
	lua_setfield(L, LUA_REGISTRYINDEX, READLINE_REGISTRY);
	return 0;
}
