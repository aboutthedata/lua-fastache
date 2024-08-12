#include <assert.h>
#include <errno.h>
#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mustache.h"

typedef struct 
{
    lua_State * Lua;
    const char * TemplateFilename;
    const char * OutputFilename;
    int LastElement;
    FILE * FilePtr;
    int ErrorCount;
} ParseInfo;

typedef struct
{
    mustache_template_t * Template;
    char TemplateFilename[1];
} LuaFastacheTemplate;

enum { false = 0, true = 1};


static void PrintIdentifier(mustache_identifier_t * pid, mustache_identifier_t * plastid)
{
    fwrite(pid->name, pid->namelen, 1, stderr);
    if(pid == plastid || !pid->subid) return;

    fputc('.', stderr);
    PrintIdentifier(pid->subid, plastid);
}


static void BeginPrintWarning(ParseInfo * ppi, int lineno)
{
    fprintf(stderr, "%s:%d: warning: ", ppi->TemplateFilename, lineno);
}

static void EndPrintWarning(ParseInfo * ppi)
{
    fprintf(stderr, "\t(while generating %s)\n", ppi->OutputFilename);
}

static void PrintVarNotFoundWarning(ParseInfo * ppi, int lineno,
                                    mustache_identifier_t * pid, mustache_identifier_t * plastid)
{
    BeginPrintWarning(ppi, lineno);
    fprintf(stderr, "Variable '");
    PrintIdentifier(pid, plastid);
    fprintf(stderr, "' not found\n");
    EndPrintWarning(ppi);
}

static int FindFirstVar(lua_State * lua, mustache_identifier_t * pid)
{
    assert(pid);
    int top = lua_gettop(lua);

    if(pid->name[0] == '.')
    { //. means use the variable itself. So just make a copy and return.
        assert(!pid->subid);
        lua_settop(lua, top+3);
        lua_pushvalue(lua, -4);
        return true;
    }
    
    int tstart = lua_istable(lua, top) ? top : top-4;
    lua_pushlstring(lua, pid->name, pid->namelen);
    
    
    for(int i = tstart; i >= 3; i -= 4)//structure is always table, next, next key, table, parent, next...
    {
        lua_pushvalue(lua, -1); //copy var name, for it's going to be popped by gettable
        lua_gettable(lua, i);
        if(!lua_isnil(lua, -1)) //found it
        {
            lua_pushnil(lua);
            lua_pushvalue(lua, -2);
            //leave result AND three other items on stack (to preserve structure: table, 3*something, table, ...)
            return true;
        }
        else lua_pop(lua, 1);
    }
    
    //not found
    lua_settop(lua, top); //restore old state
    return false;
}

static int FindVar(lua_State * lua, mustache_identifier_t * pidstart, ParseInfo * ppi, int lineno)
{
    if(!FindFirstVar(lua, pidstart))
    {
        if(pidstart->subid)
            PrintVarNotFoundWarning(ppi, lineno, pidstart, pidstart);
        return false;
    }
    
    for(mustache_identifier_t * pid = pidstart; (pid = pid->subid); )
    {
        lua_remove(lua, -2);
        lua_getfield(lua, -1, pid->name);
        if(lua_isnil(lua, -1))
        {
            if(pid->subid)
                PrintVarNotFoundWarning(ppi, lineno, pidstart, pid);
            lua_pop(lua, 4);
            return false;
        }
    }

    return true;
}

static uintmax_t GetVarCB(mustache_api_t *api, void *userdata, mustache_token_variable_t *token, uintmax_t lineno)
{
    ParseInfo * ppi = (ParseInfo*) userdata;
    lua_State * lua = ppi->Lua;
    int top = lua_gettop(lua);
    
    if(!FindVar(lua, token->identifier, ppi, lineno))
    {
        PrintVarNotFoundWarning(ppi, lineno, token->identifier, NULL);
        lua_settop(lua, top+4); //insert four nils
    }

    int ret;
    const char * str;
    size_t len;
    
    int type = lua_type(lua, -1);
    switch(type)
    {
        case LUA_TNUMBER:
        case LUA_TSTRING:
            str = lua_tolstring(lua, -1, &len);
            ret = (api->write(api, userdata, str, len) == len);
            break;
        case LUA_TBOOLEAN:
        {
            int b = lua_toboolean(lua, -1);
            str = b ? "True" : "False";
            len = b ? 4 : 5;
            ret = (api->write(api, userdata, str, len) == len);
            break;
        }
        default:
            str = lua_typename(lua, type);
            len = strlen(str);
            ret = (api->write(api, userdata, "<-<-< ", 6) == 6) 
            && (api->write(api, userdata, str, len) == len) 
            && (api->write(api, userdata, " >->->", 6) == 6);
            break;
    }
    
    lua_settop(lua, top);
    return ret;
}

static uintmax_t RenderNone(lua_State * lua, int top)
{
    lua_settop(lua, top);
    return true;
}
static uintmax_t RenderSimple(lua_State * lua, int top,
                              mustache_api_t *api, void *userdata, mustache_token_section_t *token)
{
    lua_settop(lua, top);
    return mustache_render_token(api, userdata, token->section);
}

static uintmax_t RenderTable(lua_State * lua, int top,
                             mustache_api_t *api, void *userdata, mustache_token_section_t *token)
{
    lua_pushnil(lua);
    if(!lua_next(lua, -2))
    { //no items in the list
        lua_settop(lua, top);
        return true;
    }
    lua_rotate(lua, -2, 1); //stack layout now: table -> value of first item -> key of first item

    
    ParseInfo * ppi = (ParseInfo*) userdata;
    int lastelement_backup = ppi->LastElement;
    ppi->LastElement = false;

    while(lua_next(lua, -3)) //as long as there are more items, we are not at the last one
    {
        //stack layout now: table -> value of item i-1 -> key of item i -> value of item i
        lua_pushvalue(lua, -3); //value of item i-1 on top
        if(!mustache_render_token(api, userdata, token->section)) return false;
        lua_pop(lua, 1);
        lua_replace(lua, -3); //stack layout now: table -> value of item i -> key of item i
    }

    ppi->LastElement = true;
    lua_pushnil(lua); //make sure we have the same stack structure as before (FindFirstVar relies on it)
    lua_pushnil(lua);
    lua_pushvalue(lua, -3); //value of last item on top
    if(!mustache_render_token(api, userdata, token->section)) return false;
    
    ppi->LastElement = lastelement_backup;
    lua_settop(lua, top);
    return true;
}

static uintmax_t GetSectionCB(mustache_api_t *api, void *userdata, mustache_token_section_t *token, uintmax_t lineno)
{
    ParseInfo * ppi = (ParseInfo*) userdata;
    lua_State * lua = ppi->Lua;
    int top = lua_gettop(lua);
    
    luaL_checkstack(lua, 6, "while rendering mustache template");
    
    if(token->type == SECTION_SEP)
    {
        if(ppi->LastElement) return RenderNone(lua, top);
        else return RenderSimple(lua, top, api, userdata, token);
    }
    
    int found = FindVar(lua, token->identifier, ppi, lineno);
    int booleval = found && lua_toboolean(lua, -1);
    
    switch(token->type)
    {
        case SECTION_NORMAL:
            if(!booleval) return RenderNone(lua, top);
            else if(lua_istable(lua, -1))
                return RenderTable(lua, top, api, userdata, token);
            else return RenderSimple(lua, top, api, userdata, token);
            break;
        case SECTION_INVERTED:
            if(booleval) return RenderNone(lua, top);
            else return RenderSimple(lua, top, api, userdata, token);
        case SECTION_SEP:
            assert(false); //handled before already
    }
    abort(); //this line should never be reached;
}

static uintmax_t WriteCB(mustache_api_t*, void *userdata, const char *buffer, uintmax_t buffer_size)
{
    return fwrite(buffer, 1, buffer_size, ((ParseInfo*) userdata)->FilePtr);
}

static void ErrorCB(mustache_api_t*, void *userdata, uintmax_t lineno, const char *error)
{
    ParseInfo * ppi = (ParseInfo*) userdata;
    
    if(lineno) fprintf(stderr, "%s:%d: %s\n", ppi->TemplateFilename, (int)lineno, error);
    else fputs(error, stderr);
    
    ppi->ErrorCount++;
}

static int ParseTemplate(lua_State *lua)
{
    size_t filenamelen;
    const char * filename = luaL_checklstring(lua, 1, &filenamelen);
    
    mustache_api_t api = {
        .error = &ErrorCB,
        .freedata = NULL
    };
    
    ParseInfo pi = {
        .Lua = lua,
        .TemplateFilename = filename,
        .ErrorCount = 0,
        .LastElement = false
    };
    
    LuaFastacheTemplate * template = lua_newuserdata(lua, sizeof(LuaFastacheTemplate)+filenamelen);
    template->Template = mustache_compile_file(filename, &api, &pi);
    if(pi.ErrorCount > 0 || !template->Template)
    {
        mustache_free(&api, template->Template);
        return luaL_error(lua, "%d errors occured while parsing %s", pi.ErrorCount, filename);
    }
    strcpy(template->TemplateFilename, filename);
    luaL_getmetatable(lua, "lua-fastache template");
    lua_setmetatable(lua, -2);
    
    return 1;
}

static int RenderTemplate(lua_State *lua)
{
    if(lua_gettop(lua) != 3) 
        return luaL_error(lua, "fastache.render: Wrong number of arguments (expected 3, got %d)", lua_gettop(lua));
    LuaFastacheTemplate* template = luaL_checkudata(lua, 1, "lua-fastache template");
    const char * outfilename = luaL_checkstring(lua, 2);
    luaL_checktype(lua, 3, LUA_TTABLE);

    FILE * f = fopen(outfilename, "w");
    if(!f)
    {
        char strerr[1024];
        strerror_r(errno, strerr, sizeof(strerr));
        return luaL_error(lua, "Error writing %s: %s", outfilename, strerr);
    }
    
    mustache_api_t api = {
        .write = &WriteCB,
        .error = &ErrorCB,
        .varget = &GetVarCB,
        .sectget = &GetSectionCB
    };
    
    ParseInfo pi = {
        .Lua = lua,
        .OutputFilename = outfilename,
        .TemplateFilename = template->TemplateFilename,
        .FilePtr = f,
        .LastElement = false
    };
    
    if(!mustache_render(&api, &pi, template->Template))
    {
        fclose(f);

        char strerr[1024];
        strerror_r(errno, strerr, sizeof(strerr));
        return luaL_error(lua, "Error writing to %s: %s", outfilename, strerr);
    }
    
    fclose(f);
    return 0;
}


static int DestroyTemplate(lua_State * lua)
{
    LuaFastacheTemplate* template = luaL_checkudata(lua, 1, "lua-fastache template");
    mustache_api_t api = { .freedata = NULL };
    mustache_free(&api, template->Template);
    return 0;
}

int luaopen_lua_fastache (lua_State *lua)
{
    luaL_newmetatable(lua, "lua-fastache template");
    lua_pushcfunction(lua, &DestroyTemplate);
    lua_setfield(lua, -2, "__gc");
    
        lua_newtable(lua);
        lua_pushcfunction(lua, &RenderTemplate);
        lua_setfield(lua, -2, "render");
    lua_setfield(lua, -2, "__index");
    lua_pop(lua, 1);
    
    lua_createtable(lua, 0, 2);
    lua_pushcfunction(lua, &RenderTemplate);
    lua_setfield(lua, -2, "render");
    lua_pushcfunction(lua, &ParseTemplate);
    lua_setfield(lua, -2, "parse");
    
    lua_pushvalue(lua, -1);
    lua_setglobal(lua, "fastache");
    return 1;
}
