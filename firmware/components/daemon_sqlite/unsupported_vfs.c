// Compile/readiness gate only. Persistent files are explicitly unsupported.
// No FatFS locking or sync semantics are invented by this VFS.
#include "sqlite3.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h>

static int unsupported_open(sqlite3_vfs* vfs,const char* name,sqlite3_file* file,int flags,int* output_flags) {
    (void)vfs; (void)name; (void)file; (void)flags; (void)output_flags;
    return SQLITE_CANTOPEN;
}
static int unsupported_delete(sqlite3_vfs* vfs,const char* name,int sync) {
    (void)vfs; (void)name; (void)sync; return SQLITE_IOERR_DELETE;
}
static int no_access(sqlite3_vfs* vfs,const char* name,int flags,int* result) {
    (void)vfs; (void)name; (void)flags; *result=0; return SQLITE_OK;
}
static int full_path(sqlite3_vfs* vfs,const char* name,int size,char* result) {
    (void)vfs;
    if ((int)strlen(name)>=size) return SQLITE_CANTOPEN;
    memcpy(result,name,strlen(name)+1); return SQLITE_OK;
}
static int random_bytes(sqlite3_vfs* vfs,int size,char* output) {
    (void)vfs; esp_fill_random(output,(size_t)size); return size;
}
static int sleep_us(sqlite3_vfs* vfs,int microseconds) {
    (void)vfs;
    const TickType_t ticks=pdMS_TO_TICKS((microseconds+999)/1000);
    vTaskDelay(ticks ? ticks : 1); return microseconds;
}
static int current_time(sqlite3_vfs* vfs,sqlite3_int64* value) {
    (void)vfs; struct timeval now; gettimeofday(&now,0);
    *value=210866760000000LL+(sqlite3_int64)now.tv_sec*1000+now.tv_usec/1000;
    return SQLITE_OK;
}
static sqlite3_vfs gate={
    .iVersion=2,.szOsFile=sizeof(sqlite3_file),.mxPathname=128,.zName="daemon-unsupported-storage",
    .xOpen=unsupported_open,.xDelete=unsupported_delete,.xAccess=no_access,.xFullPathname=full_path,
    .xRandomness=random_bytes,.xSleep=sleep_us,.xCurrentTimeInt64=current_time
};
int sqlite3_os_init(void) { return sqlite3_vfs_register(&gate,1); }
int sqlite3_os_end(void) { return SQLITE_OK; }
