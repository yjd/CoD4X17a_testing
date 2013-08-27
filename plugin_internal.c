#include "plugin_handler.h"


/*==========================================*
 *                                          *
 *   Plugin Handler's internal functions    *
 *                                          *
 * Functions in this file might be not safe *
 *    for use outside of plugin handler.    *
 *                                          *
 *==========================================*/



/* 
============
 TCP module
============
*/

qboolean PHandler_TcpConnect(int pID, const char* remote, int connection)
{
    if(pluginFunctions.plugins[pID].sockets[connection].sock < 1){
        pluginFunctions.plugins[pID].sockets[connection].sock = NET_TcpClientConnect( remote );

        if(pluginFunctions.plugins[pID].sockets[connection].sock < 1){
            Com_Printf("Plugins: Notice! Error connecting to server: %s for plugin #%d!\n", remote, pID);
            return qfalse;
        }
        NET_StringToAdr(remote, &pluginFunctions.plugins[pID].sockets[connection].remote, NA_UNSPEC);
        return qtrue;
    }
    Com_PrintError("Plugin_TcpConnect: Connection id %d is already in use for plugin #%d!\n",connection ,pID );

    return qfalse;
}

int PHandler_TcpGetData(int pID, int connection, void* buf, int size )
{
    int len;
    pluginTcpClientSocket_t* ptcs = &pluginFunctions.plugins[pID].sockets[connection];

    if(ptcs->sock < 1){
        Com_PrintWarning("Plugin_TcpGetData: called on a non open socket for plugin ID: #%d\n", pID);
        return -1;
    }
    len = NET_TcpClientGetData(ptcs->sock, buf, size);

    if(len == -1)
    {
        ptcs->sock = -1;
    }

    return len;
}

qboolean PHandler_TcpSendData(int pID, int connection, void* data, int len)
{
    int state;

    pluginTcpClientSocket_t* ptcs = &pluginFunctions.plugins[pID].sockets[connection];

    if(ptcs->sock < 1){
        Com_PrintWarning("Plugin_TcpSendData: called on a non open socket for plugin ID: #%d\n", pID);
        return qfalse;
    }
    state =  NET_TcpSendData(ptcs->sock, data, len);

    if(state == -1)
    {
        ptcs->sock = -1;
        return qfalse;
    }
    return qtrue;
}

void PHandler_TcpCloseConnection(int pID, int connection)
{
    pluginTcpClientSocket_t* ptcs = &pluginFunctions.plugins[pID].sockets[connection];

    if(ptcs->sock < 1){
        Com_PrintWarning("Plugin_TcpCloseConnection: Called on a non open socket for plugin ID: #%d\n", pID);
        return;
    }
    NET_TcpCloseSocket(ptcs->sock);
    ptcs->sock = -1;
}

/* 
=====================================
 Functionality providers for exports
=====================================
*/

void PHandler_ChatPrintf(int slot, char *fmt, ... )
{
    char str[256];
    client_t *cl;
    va_list vl;

    cl = slot >=0 ? &(svs.clients[slot]) : NULL;
    va_start(vl,fmt);
    vsprintf(str,fmt,vl);
    va_end(vl);
    SV_SendServerCommand(cl, "h \"%s\"", str);
}
void PHandler_BoldPrintf(int slot, char *fmt, ... )
{
    char str[256];
    client_t *cl;
    va_list vl;
    cl = slot >=0 ? &(svs.clients[slot]) : NULL;
    va_start(vl,fmt);
    vsprintf(str,fmt,vl);
    va_end(vl);
    SV_SendServerCommand(cl, "c \"%s\"", str);
}

void PHandler_CmdExecute_f()
{
    Com_DPrintf("Attempting to execute a plugin command '%s'.\n",Cmd_Argv(0));
    if(!pluginFunctions.enabled){
        Com_DPrintf("Error! Tried executing a plugin command with plugins being disabled! Command name: '%s'.\n",Cmd_Argv(1));
        return;
    }
    char name[128];
    int i,j;
    void (*func)();
    strcpy(name,Cmd_Argv(0));
    for(i=0;i<MAX_PLUGINS;i++){
        if(pluginFunctions.plugins[i].loaded && pluginFunctions.plugins[i].enabled){
            for(j=0;j<pluginFunctions.plugins[i].cmds;j++)
                if(strcmp(name,pluginFunctions.plugins[i].cmd[j].name)==0){
                    Com_DPrintf("Executing plugin command '%s' for plugin '%s', plugin ID: %d.\n",name,pluginFunctions.plugins[i].name,i);
                    func = (void (*)())(pluginFunctions.plugins[i].cmd[j].xcommand);

                    func();
                    return;
                    }
        }
    }
}

void PHandler_RemoveCommand(int pID,char *name)
{
    int i,j,k;
    j=pluginFunctions.plugins[pID].cmds;
    for(i=0;i<j;i++){
        if(strcmp(name,pluginFunctions.plugins[pID].cmd[i].name)==0){
            Cmd_RemoveCommand(name);
            memset(pluginFunctions.plugins[pID].cmd,0x00,sizeof(pluginCmd_t));
            // Now we need to rearrrange the array...
            for(k=i;k<j-1;k++){
                pluginFunctions.plugins[pID].cmd[k] = pluginFunctions.plugins[pID].cmd[k+1];

            }
            Com_DPrintf("Command '%s' removed for plugin %s.\n",name,pluginFunctions.plugins[pID].name);
            return;
        }

    }
    Com_DPrintf("Warning: tried removing command '%s', which was not found for plugin %s.\n",name,pluginFunctions.plugins[pID].name);

}

void *PHandler_Malloc(int pID,size_t size)
{
    int i;
    Com_DPrintf("Attempting to allocate %dB of memory for plugin #%d...\n",size,pID);
    //Plugin identified, find the first free spot in it's allocated pointers table
    for(i=0;i<PLUGIN_MAX_MALLOCS;i++){
        if(pluginFunctions.plugins[pID].memory[i].ptr==NULL){
            pluginFunctions.plugins[pID].memory[i].ptr = malloc(size);
            pluginFunctions.plugins[pID].memory[i].size = size;
            pluginFunctions.plugins[pID].usedMem += size;
            ++pluginFunctions.plugins[pID].mallocs;
            Com_DPrintf("Allocating %dB of memory for plugin #%d.\n",size,pID);
            return pluginFunctions.plugins[pID].memory[i].ptr;

        }

    }
    Com_Printf("Plugins: Warning! Memory allocations limit reached for plugin #%d!\n",pID);
    return NULL;
}
void PHandler_Free(int pID, void *ptr)
{

    int i;
    if(ptr==NULL){
        Com_DPrintf("Plugins: Warning! Plugin #%d tried freeing a NULL pointer! Called Plugin_Free() twice?\n",pID);
        return;
    }
    //Plugin identified, find the first free spot in it's allocated pointers table
    for(i=0;i<PLUGIN_MAX_MALLOCS;i++){
        if(pluginFunctions.plugins[pID].memory[i].ptr==ptr){
            free(ptr);
            pluginFunctions.plugins[pID].memory[i].ptr = NULL;
            pluginFunctions.plugins[pID].usedMem -= pluginFunctions.plugins[pID].memory[i].size;
            --pluginFunctions.plugins[pID].mallocs;
            return;
        }
    }
    Com_DPrintf("Plugins: Warning! Plugin %d tried freeing an unknown pointer!\n",pID);

}

void PHandler_FreeAll(int pID)
{
    int i;
    if(pID<0){
        Com_Printf("Plugins: Error! Tried to free all memory of an unknown plugin!\n");
        return;
    }
    for(i=0;i<PLUGIN_MAX_MALLOCS;++i){
        if(pluginFunctions.plugins[pID].memory[i].ptr!=NULL){
            free(pluginFunctions.plugins[pID].memory[i].ptr);
            pluginFunctions.plugins[pID].memory[i].ptr=NULL;
        }
    }
    pluginFunctions.plugins[pID].usedMem = 0;
    pluginFunctions.plugins[pID].mallocs = 0;
    Com_DPrintf("Plugins: Memory for plugin #%d has been freed.\n",pID);

}
P_P_F void PHandler_Error(int pID,int code,char *string)
{
    if(pluginFunctions.plugins[pID].enabled==qfalse){
        Com_PrintWarning("An error of ID %d and string \"%s\" occured in a disabled plugin with ID %d!\n",code,string,pID);
        return;
    }
    switch(code)
    {
        case P_ERROR_WARNING:
            Com_Printf("Plugin #%d ('%s') issued a warning: \"%s\"\n",pID,pluginFunctions.plugins[pID].name, string);
            break;
        case P_ERROR_DISABLE:
            Com_Printf("Plugin #%d ('%s') returned an error and will be disabled! Error string: \"%s\".\n",pID,pluginFunctions.plugins[pID].name,string);
            pluginFunctions.plugins[pID].enabled = qfalse;
            break;
        case P_ERROR_TERMINATE:
            Com_Printf("Plugin #%d ('%s') reported a critical error, the server will be terminated. Error string: \"%s\".\n",pID,pluginFunctions.plugins[pID].name,string);
            Com_Error(ERR_FATAL,string);
            break;
        default:
            Com_DPrintf("Plugin #%d ('%s') reported an unknown error! Error string: \"%s\", error code: %d.\n",pID,pluginFunctions.plugins[pID].name,string,code);
            break;
    }


}
/*
=================
 Server commands
=================
*/
void PHandler_LoadPlugin_f( void )
{
        if( Cmd_Argc() < 2){
                Com_Printf("Usage: %s <plugin file name without extension>\n", Cmd_Argv(0));
                return;
        }
        PHandler_Load(Cmd_Argv(1),128);
}
void PHandler_UnLoadPlugin_f()
{
    if( Cmd_Argc() < 2){
        Com_Printf("Usage: %s <plugin file name without extension>\n", Cmd_Argv(0));
        return;
    }
    PHandler_UnloadByName(Cmd_Argv(1),128);
}
void PHandler_PluginInfo_f()
{
    if(Cmd_Argc() < 2){
        Com_Printf("Usage: %s <plugin name>\n",Cmd_Argv(0));
        return;
    }
    int id = PHandler_GetID(Cmd_Argv(1),strlen(Cmd_Argv(1)));
    int i;
    int vMajor, vMinor;
    pluginInfo_t info;
    if(id<0){
        Com_Printf("Plugin \"%s\" is not loaded!\n",Cmd_Argv(1));
        return;
    }
    (*pluginFunctions.plugins[id].OnInfoRequest)(&info);
    Com_Printf("\n");
    Com_Printf("\n^2Plugin name:^7\n%s\n\n",pluginFunctions.plugins[id].name);
    vMajor = info.pluginVersion.major;
    vMinor = info.pluginVersion.minor;
    if(vMinor > 100){
        while(vMinor>=1000){
            vMinor /= 10;
        }
    }
    Com_Printf("\n^2Plugin version:^7\n%d.%d\n\n",vMajor,vMinor);
    Com_Printf("\n^2Full plugin name:^7\n%s\n\n",info.fullName);
    Com_Printf("\n^2Short plugin description:^7\n%s\n\n",info.shortDescription);
    Com_Printf("\n^2Full plugin description:^7\n%s\n\n",info.longDescription);
    Com_Printf("\n^2Plugin commands:^7\n\n");
    for(i=0;i<pluginFunctions.plugins[id].cmds;++i){
        Com_Printf(" -%s\n",pluginFunctions.plugins[id].cmd[i].name);
    }
    Com_Printf("\n^2Total of %d commands.^7\n\n",pluginFunctions.plugins[id].cmds);
}
void PHandler_PluginList_f()
{
    int i,j;
    if(pluginFunctions.loadedPlugins == 0){
        Com_Printf("No plugins are loaded.\n");
    }
    else{
        Com_Printf("\nLoaded plugins:\n\n");
        Com_Printf("*----------------------------------------------------------------------------------*\n");
        Com_Printf("| ID |         name         | enabled? | memory allocations | total all. mem. in B |\n");
        for(i=0,j=0;i<pluginFunctions.loadedPlugins;++i,++j){
            while(j<MAX_PLUGINS){    // ORing might be dangerous when the compiler uses optimalization...
                if(pluginFunctions.plugins[j].loaded)
                    break;
                ++j;
            }
            if(j==MAX_PLUGINS){
                i=j;
                break;
            }
            Com_Printf("| %-3d| %-21s| %-9s| %-19d| %-21d|\n",j,pluginFunctions.plugins[j].name,pluginFunctions.plugins[j].enabled==0 ? "no" : "yes",pluginFunctions.plugins[j].mallocs,pluginFunctions.plugins[j].usedMem);

        }
        
        Com_Printf("*----------------------------------------------------------------------------------*\n");
        Com_Printf("\nTotal of %d loaded plugins.\n",i);
    }
    
    Com_Printf("\nPlugin handler version: %d.%d.\n", PLUGIN_HANDLER_VERSION_MAJOR, PLUGIN_HANDLER_VERSION_MINOR);

}
/*
======
 Misc
======
*/

P_P_F int PHandler_CallerID() // P_P_F for no-inline :P
{
    void *funcptrs[3];
    int i,j;
    j = backtrace(funcptrs,3);
    if(j<3){
    Com_Error(ERR_FATAL,"PHandler_CallerID: backtrace failed to return function pointers! Possible exploit detected! Terminating the server...\n");
    }
    for(i=0;i<MAX_PLUGINS;++i){
        if(pluginFunctions.plugins[i].lib_start < funcptrs[2] && pluginFunctions.plugins[i].lib_start + pluginFunctions.plugins[i].lib_size > funcptrs[2])
            return i;
    }
    return -1;
}