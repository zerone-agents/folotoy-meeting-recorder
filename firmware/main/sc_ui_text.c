#include "sc_ui_text.h"
#include <string.h>
typedef struct {const char *en,*zh;} text_pair;
static const text_pair messages[]={
 {"Busy: Wi-Fi export","正在导出文件"},{"Mic unavailable","麦克风不可用"},
 {"Storage unavailable","存储不可用"},{"Storage full","存储空间已满"},
 {"Metadata error - cannot record","录音信息异常"},{"Cannot create file","无法创建录音"},
 {"Stopping upload...","正在停止上传"},{"Mic stop pending","等待停止录音"},
 {"Storage write pending","等待写入存储"},{"Storage sync failed","存储同步失败"},
 {"Save failed - retained","保存失败，文件保留"},{"Incomplete audio saved","录音不完整，已保存"},
 {"Wi-Fi export unavailable","无法导出文件"},{"Wi-Fi failed","网络连接失败"},
 {"Paired","已配对"},{"Reset unavailable","无法重置"},{"Recording (app)","正在录音"},
 {"Start cancelled","已取消录音"},{"Delete unavailable","暂时无法删除"},
 {"Stop recording first","请先停止录音"},{"Bad Wi-Fi config","网络配置错误"},
 {"Press OK to pair","按 OK 配对"},{"Unbound","已解除绑定"},
 {"Setup unavailable","暂时无法配置"},{"Bluetooth stop failed","蓝牙停止失败"},{"Bluetooth busy","蓝牙忙，录音失败"},
 {"Audio busy","录音设备正忙"},{"Setup failed","配置失败"},
 {"Setup stop failed","停止配置失败"},{"Recording","正在录音"},
 {"Review before OK","请先核对信息"},{"Clock not trusted - setup failed","时间未确认，配置失败"},
 {"Setup failed - not active","配置失败，未启用"},{"Review clock setup","请核对时间"},
 {"Saved - awaiting PC","已保存，等待电脑"},{"Pairing cancelled","已取消配对"},
 {"Paused","已暂停"},{"Wi-Fi cancelled","已取消网络连接"},{"Saved","已保存"},
 {"30 minute limit - saved","已达 30 分钟，已保存"},{"Storage full - saved","空间已满，已保存"},
 {"Upload enabled","上传已启用"},{"Network start failed","网络启动失败"},
 {"Upload busy - start failed","上传正忙，录音失败"},{"Pairing timed out","配对超时"},
 {"Wi-Fi export done","文件导出完成"},{"Wi-Fi timeout","网络连接超时"},
 {"Wi-Fi idle","网络已停止"},{"App connected","应用已连接"},
 {"Cannot review now","暂时无法检查"},{"Review unavailable","无法检查录音"},
 {"Check status; retry","检查状态后重试"},{"Delete recovery pending","等待完成删除"},
 {"Delete queued","正在删除"},{"Cleanup needs attention","清理需要处理"},
 {"Empty recording retained","空录音已保留"},{"Delete recovery needs attention","删除需要处理"}
};
const char *sc_ui_toast_text(const char *message){
 if(!message)return "";
 for(size_t i=0;i<sizeof(messages)/sizeof(messages[0]);i++)if(!strcmp(message,messages[i].en))return messages[i].zh;
 return message;
}
