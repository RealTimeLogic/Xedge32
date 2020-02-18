/*
 *     ____             _________                __                _     
 *    / __ \___  ____ _/ /_  __(_)___ ___  ___  / /   ____  ____ _(_)____
 *   / /_/ / _ \/ __ `/ / / / / / __ `__ \/ _ \/ /   / __ \/ __ `/ / ___/
 *  / _, _/  __/ /_/ / / / / / / / / / / /  __/ /___/ /_/ / /_/ / / /__  
 * /_/ |_|\___/\__,_/_/ /_/ /_/_/ /_/ /_/\___/_____/\____/\__, /_/\___/  
 *                                                       /____/          
 *
 *                  Barracuda Embedded Web-Server 
 ****************************************************************************
 *
 *   $Id: LspAppMgr.c 4492 2020-02-10 18:12:19Z wini $
 *
 *   COPYRIGHT:  Real Time Logic, 2008 - 2020
 *               http://www.realtimelogic.com
 *
 *   The copyright to the program herein is the property of
 *   Real Time Logic. The program may be used or copied only
 *   with the written permission from Real Time Logic or
 *   in accordance with the terms and conditions stipulated in
 *   the agreement under which the program has been supplied.
 ****************************************************************************


LSP Application Manager Documentation (lspappmgr):
http://realtimelogic.com/ba/doc/?url=/examples/lspappmgr/readme.html

Note: the lspappmgr includes files and resources from the auxiliary
API -- i.e. from the xrc directory. The makefile includes these files in the
build process. The makefile also assembles a number of resource files,
such as Lua scripts, HTML files, etc, and assembles these files into a
ZIP file.

The extra optional API's included are documented in the startup code below.

*/

/*
  Include all I/O related header files
*/
#include <barracuda.h>

/* 
   io=ezip
*/
#if !defined(BAIO_DISK) && !defined(BAIO_ZIP)
/* Default is: BAIO_EZIP */
extern ZipReader* getLspZipReader(void);
#endif

#ifdef AUX_LUA_BINDINGS
AUX_LUA_BINDINGS_DECL
#else
#define AUX_LUA_BINDINGS
#endif
extern void luaopen_esp(lua_State* L); /* Example Lua binding: led.c */

/* Some operating systems such as ThreadX/FileX require special initialization.
 * This variable can be set to a function by platform specific startup code.
 * (REF-1).
*/
#ifndef NO_BAIO_DISK
int (*platformInitDiskIo)(DiskIo*);

/* Example code */
#ifdef _WIN32
#include <stdlib.h>
static int initDiskIo(DiskIo* io)
{
   DiskIo_setRootDir(io,getenv("USERPROFILE"));
   return 0;
}
#elif defined(BA_POSIX)
#include <stdlib.h>
static int initDiskIo(DiskIo* io) {
   DiskIo_setRootDir(io,getenv("HOME"));
   return 0;
}
#elif defined(ESP_PLATFORM)
/* ESP32 */
#include<esp_vfs.h>
#include<esp_vfs_fat.h>
#include<esp_system.h>
static int initDiskIo(DiskIo* io)
{
   static const char bp[] = {"/spiflash"}; 
   const esp_vfs_fat_mount_config_t mcfg = {
      .max_files = 20,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
   };
   static wl_handle_t hndl = WL_INVALID_HANDLE;
   esp_err_t err = esp_vfs_fat_spiflash_mount(bp, "storage", &mcfg, &hndl); 
   if (err != ESP_OK)
   {
      HttpTrace_printf(0,"Failed to mount FATFS (%s)", esp_err_to_name(err));
      return -1;
   }
   DiskIo_setRootDir(io,bp);
   return 0;
}
#endif
#endif /* NO_BAIO_DISK */

/* Declare and call the server's main loop from main() or a thread */
void barracuda(void);


/* Initialize the HttpServer object by calling HttpServer_constructor.
   The HttpServerConfig object is only needed during initialization.
*/
static void
createServer(HttpServer* server, SoDisp* dispatcher)
{
   HttpServerConfig scfg;

   HttpServerConfig_constructor(&scfg);

   /* (A) Configure for 3 threads. See HttpCmdThreadPool. */
   HttpServerConfig_setNoOfHttpCommands(&scfg,3);

   HttpServerConfig_setNoOfHttpConnections(&scfg,20);

   /* For huge url encoded data, if any. */
   HttpServerConfig_setRequest(&scfg,2*1024, 8*1024);
   /* Large response buffers makes the NetIO much faster when used by
    * the web-server.
    */
   HttpServerConfig_setResponseData(&scfg,8*1024);

   /* Create and init the server, by using the above HttpServerConfig.
    */
   HttpServer_constructor(server, dispatcher, &scfg);
}

/* #define ENABLE_LUA_TRACE */
/* Prints every line executed by Lua if enabled */
#ifdef ENABLE_LUA_TRACE
static void lHook(lua_State *L, lua_Debug *ar)
{
   lua_getinfo(L, "S", ar);
   printf("%4d\t%s\n",ar->currentline,ar->short_src);
}
#endif


/*
  This is the "main" barracuda function. This function initializes the
  web-server, configures a Lua Virtual Machine and enters a forever
  loop.

  This function is typically called from a thread/task dedicated to
  the Barracuda web-server.

  Notice that we do not create a Virtual File System in this startup
  code. The Virtual File System is created by the Lua .preload script
  in the "lsp" subdirectory. The .preload script is executed when we
  call balua_loadconfig from the C code below.
*/
void
barracuda(void)
{
   ThreadMutex mutex;
   SoDisp dispatcher;
   HttpServer server;
   BaTimer timer;
   HttpCmdThreadPool pool;
   int ecode;
   NetIo netIo;
   lua_State* L; /* pointer to a Lua virtual machine */
   BaLua_param blp = {0}; /* configuration parameters */

   /* vmIo=
         disk   |    net   |    zip   |   ezip
      BAIO_DISK | BAIO_NET | BAIO_ZIP | BAIO_EZIP
    */
#if defined(BAIO_DISK)
   static DiskIo vmIo;
   DiskIo_constructor(&vmIo);
   if( (ecode=DiskIo_setRootDir(&vmIo, "../LspZip")) != 0 )
   {
      HttpTrace_printf(0, "Cannot set DiskIo ../LspZip directory: %s\n",
                       baErr2Str(ecode));
      baFatalE(FE_USER_ERROR_1, 0);
   }
#elif defined(BAIO_NET)
#error Cannot use the NetIo: Not implemented.
/* Disabled Makefile option: make io=net
   Think about the LSP Application Manager as a tool and the tool's
   web and Lua app as firmware. The lua code is required for the
   operation of the tool and should be embedded in the executable. The
   code is designed to be used "as is" as a development tool and you
   do not need to study or modify it (unless you want to know how the
   Lua code works).
*/
#else
   static ZipIo vmIo;
   ZipReader* zipReader;
#if defined(BAIO_ZIP)
   static FileZipReader fileZipReader;
   FileZipReader_constructor(&fileZipReader, "lsp.zip");
   zipReader = (ZipReader*)&fileZipReader;  /* cast to base class */
#else
   /* BAIO_EZIP */
   zipReader = getLspZipReader();
#endif
   if( ! CspReader_isValid((CspReader*)zipReader) )
      baFatalE(FE_USER_ERROR_2, 0);
   ZipIo_constructor(&vmIo, zipReader, 2048, 0);
   if(ZipIo_getECode(&vmIo) !=  ZipErr_NoError)
      baFatalE(FE_USER_ERROR_3, 0);
#endif

/* Example code for 'platformInitDiskIo declaration' (REF-1).
   This would typically be in another (startup) file for deep embedded systems.
*/
#if !defined(NO_BAIO_DISK) && \
   (defined(_WIN32) || \
    defined(BA_POSIX) || \
    defined(ESP_PLATFORM))
   platformInitDiskIo=initDiskIo;
#endif

   /* Create the Socket dispatcher (SoDisp), the SoDisp mutex, and the server.
    */
   ThreadMutex_constructor(&mutex);
   SoDisp_constructor(&dispatcher, &mutex);
   createServer(&server, &dispatcher);

   /* For embedded systems without a file system */
   NetIo_constructor(&netIo, &dispatcher);

   /* The optional timer is useful in LSP code */
   BaTimer_constructor(&timer, &mutex, BA_STACKSZ, 25, ThreadPrioNormal, 0);
   
   /* Create a LSP virtual machine.
    */
   blp.vmio = (IoIntf*)&vmIo;  /* The required IO */
   blp.server = &server;           /* pointer to a HttpServer */
   blp.timer = &timer;             /* Pointer to a BaTimer */
   L = balua_create(&blp);        /* create the Lua state */

   /* Install optional IO interfaces */
   balua_iointf(L, "net",  (IoIntf*)&netIo);
   balua_http(L); /* Install optional HTTP client library */
   balua_thread(L); /* Install optional Lua thread library */
   balua_socket(L);  /* Install optional Lua socket library */
   balua_sharkssl(L);  /* Install optional Lua SharkSSL library */
   balua_crypto(L);  /* Install optional crypto library */
   balua_tracelogger(L); /* Install optional trace logger library */
   luaopen_esp(L); /* Example Lua binding: led.c */
   AUX_LUA_BINDINGS /* Expands to nothing if not set */

/* ezip is for release of the LSP app. mgr.
   Some embedded devices may not have a DiskIo.
   The following macro makes it possible to disable the DiskIo.
   Command line: make ..... nodisk=1
*/
#ifndef NO_BAIO_DISK
   {
      static DiskIo diskIo;
      DiskIo_constructor(&diskIo);
      /* REF-1 */
      if( ! platformInitDiskIo || (*platformInitDiskIo)(&diskIo) == 0)
      {
         /* Add optional IO interfaces */
         balua_iointf(L, "disk",  (IoIntf*)&diskIo);
      }
   }
#endif


   /* Create user/login tracker */
   balua_usertracker_create(
      L,
      20, /* Tracker node size. */
      4, /* Max number of login attempts. */
      10*60); /* 10 minutes ban time if more than 4 login attempts in a row. */

   /* Dispatcher mutex must be locked when running the .config script
    */
   ThreadMutex_set(&mutex);
   ecode=balua_loadconfig(L, (IoIntf*)&vmIo, 0); /* Load and run .config  */
   ThreadMutex_release(&mutex);

#ifdef ENABLE_LUA_TRACE
   lua_sethook(L, lHook, LUA_MASKLINE, 0);
#endif

   if(ecode)
   {
      HttpTrace_printf(0,".config error: %s.\n", lua_tostring(L,-1)); 
      baFatalE(FE_USER_ERROR_2, 0);
   }

   /* See (A) above */
   HttpCmdThreadPool_constructor(&pool, &server, ThreadPrioNormal, BA_STACKSZ);

   /*
     The dispatcher object waits for incoming HTTP requests. These
     requests are sent to the HttpServer object, where they are delegated to
     a Barracuda resource such as the WebDAV instance.

     Note: the server socket connections are opened by the Lua script
     .config and not by C code.
   */
   SoDisp_run(&dispatcher, -1);  /* -1: Never return */
}
