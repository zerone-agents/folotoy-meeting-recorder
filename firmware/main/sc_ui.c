/* Meeting recorder interface. All LVGL work is in the LVGL timer task. */
#include "sc_ui.h"
#include "sc_ui_text.h"
#include "sc_audio.h"
#include "sc_core.h"
#include "sc_proto.h"
#include "sc_store.h"
#include "lvgl.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
LV_FONT_DECLARE(meeting_font_16);

#define BG 0x111720
#define INK 0xF4F2EA
#define MUTED 0xAAB6BD
#define BLUE 0x6AC5D8
#define ALERT 0xFF866B
#define RING_TRACK 0x546B78
#define RING_DIAMETER 152
#define RING_WIDTH 10
#define ROWS 6
static atomic_int page_request;
static atomic_int selected_index=ATOMIC_VAR_INIT(-1);
static atomic_uint displayed_selection_id;
static atomic_bool keep_awake;
static lv_obj_t *title,*body,*config_wifi_row,*config_wifi_name,*config_ble_row,*config_ble_name,*footer,*network_icon,*wifi_slash,*battery_fill,*battery_unknown;
static lv_obj_t *recording_panel, *recording_ring, *recording_clock, *recording_state;
static uint8_t displayed_record_status = UINT8_MAX;
static uint32_t displayed_record_seconds = UINT32_MAX;
/* Recording metadata is immutable after creation; avoid reading NVS every
 * 200 ms from the display task. File IDs are never reused. */
typedef struct { uint32_t id; int64_t epoch; bool known; } display_time;
static display_time display_times[ROWS];
static size_t display_start=(size_t)-1;
static const char *upload_detail_text(meeting_network_state state)
{
    switch(state){
    case MEETING_NETWORK_CLOCK_REQUIRED:return "等待同步时间";
    case MEETING_NETWORK_SYNCING:return "正在上传";
    case MEETING_NETWORK_RETRY:return "等待重新上传";
    case MEETING_NETWORK_STOPPING:return "正在停止上传";
    default:return "";
    }
}

static lv_obj_t *make_text(lv_obj_t *parent,const lv_font_t *font,int x,int y,int width,uint32_t color)
{
    lv_obj_t *obj=lv_label_create(parent);
    lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,width);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(obj,5,0);
    lv_label_set_text(obj,"");
    return obj;
}

static void refresh_recording_level(lv_timer_t *timer)
{
    (void)timer;
    if (displayed_record_status != SC_REC_RECORDING ||
        lv_obj_has_flag(recording_panel, LV_OBJ_FLAG_HIDDEN)) return;

    lv_arc_set_value(recording_ring, sc_audio_level());
}

static void refresh_recording_panel(const sc_snapshot_t *snapshot)
{
    if (displayed_record_status != snapshot->rec_status) {
        displayed_record_status = snapshot->rec_status;
        bool recording = displayed_record_status == SC_REC_RECORDING;
        bool paused = displayed_record_status == SC_REC_PAUSED;
        lv_label_set_text(recording_state, recording ? "录音中" : paused ? "已暂停" : "准备录音");
        lv_obj_set_style_text_color(recording_state, lv_color_hex(paused ? ALERT : MUTED), 0);
        lv_arc_set_value(recording_ring, recording ? sc_audio_level() : 0);
    }
    if (displayed_record_seconds != snapshot->rec_seconds) {
        displayed_record_seconds = snapshot->rec_seconds;
        lv_label_set_text_fmt(recording_clock, "%02lu:%02lu", (unsigned long)(displayed_record_seconds / 60),
                              (unsigned long)(displayed_record_seconds % 60));
    }
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;sc_snapshot_t s;sc_core_snapshot(&s);
    bool setup=s.setup.state!=MEETING_SETUP_IDLE;
    bool wifi=s.link>=SC_LINK_WIFI_PENDING;
    atomic_store(&keep_awake,setup||wifi);
    bool wifi_connecting=!s.wifi_has_ip&&s.connected_ssid[0];
    bool show_slash=!s.config_ble_active&&!s.wifi_has_ip&&!wifi_connecting;
    lv_label_set_text(network_icon,s.config_ble_active?LV_SYMBOL_BLUETOOTH:LV_SYMBOL_WIFI);
    bool blink=s.config_ble_active?!s.config_ble_authenticated:wifi_connecting;
    lv_obj_set_style_opa(network_icon,
        blink&&(lv_tick_get()/500)%2?LV_OPA_TRANSP:LV_OPA_COVER,0);
    if(show_slash)lv_obj_remove_flag(wifi_slash,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(wifi_slash,LV_OBJ_FLAG_HIDDEN);
    if(s.battery_soc>=0&&s.battery_soc<=100){
        lv_obj_add_flag(battery_unknown,LV_OBJ_FLAG_HIDDEN);
        if(s.battery_soc==0)lv_obj_add_flag(battery_fill,LV_OBJ_FLAG_HIDDEN);
        else {lv_obj_set_width(battery_fill,(s.battery_soc*18+99)/100);lv_obj_remove_flag(battery_fill,LV_OBJ_FLAG_HIDDEN);}
    }else{
        lv_obj_add_flag(battery_fill,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(battery_unknown,LV_OBJ_FLAG_HIDDEN);
    }
    int page=atomic_load(&page_request);
    bool delete_visible = s.delete_review_busy || s.delete_review.stage != MEETING_DELETE_UI_IDLE;
    bool normal_page = !setup && !wifi && s.storage_ready && !delete_visible;
    if (normal_page && page == 0) lv_obj_remove_flag(recording_panel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(recording_panel, LV_OBJ_FLAG_HIDDEN);
    if(page!=2||!normal_page){
        lv_obj_add_flag(config_wifi_row,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(config_wifi_name,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(config_ble_row,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(config_ble_name,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(body, normal_page && page == 0 ? 237 : 95);
    }
    if(setup){
        if(s.setup.state==MEETING_SETUP_PAIRING){
            lv_label_set_text(title,"安全连接");
            lv_label_set_text_fmt(body,"在电脑上输入配对码:\n\n%s\n\n等待配置...",s.setup.code);
            lv_label_set_text(footer,"长按 OK 取消");
        }else if(s.setup.state==MEETING_SETUP_REVIEW){
            char hash[65];
            for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02X",s.setup.ca_sha256[i]);
            lv_label_set_text(title,"确认配置");
            lv_label_set_text_fmt(body,"CA SHA-256:\n%.16s\n%.16s\n%.16s\n%.16s\n\nUTC: %lld",
                                  hash,hash+16,hash+32,hash+48,(long long)s.setup.utc);
            lv_label_set_text(footer,"OK 确认 | 长按取消");
        }else if(s.setup.state==MEETING_SETUP_FIRST_CLOCK){
            lv_label_set_text(title,"首次设置时间");
            lv_label_set_text_fmt(body,"没有保存时间下限。\n请核对电脑时间。\n\nUTC: %lld\n\n手动配置",
                                  (long long)s.setup.utc);
            lv_label_set_text(footer,"OK 确认 | 长按取消");
        }else{
            lv_label_set_text(title,"配置已保存");
            lv_label_set_text(body,"等待电脑确认。");
            lv_label_set_text(footer,"长按 OK 取消");
        }
        return;
    }
    if(!s.storage_ready){
        lv_label_set_text(title,"存储不可用");
        lv_label_set_text(body,"可能有录音。\n\n请使用恢复工具。\n不要格式化存储。");
        lv_label_set_text(footer,"无法录音");return;
    }
    if(delete_visible){
        lv_label_set_text(title,s.delete_review.stage==MEETING_DELETE_UI_CONFIRM?"确认删除":"删除录音");
        if(s.delete_commit_busy)lv_label_set_text(body,"正在准备删除...\n\n请稍候。");
        else if(s.delete_review_busy)lv_label_set_text(body,"正在检查录音...\n\n长按 OK 取消。");
        else if(s.delete_review.visibility==MEETING_DELETE_CONFIRMED)
            lv_label_set_text(body,"服务器已确认。\n删除设备录音后\n服务器记录会保留。");
        else if(s.delete_review.visibility==MEETING_DELETE_UNCONFIRMED)
            lv_label_set_text(body,"尚未收到确认。\n服务器可能有数据。\n设备将停止上传。");
        else lv_label_set_text(body,"服务器状态未知。\n将删除设备录音。\n服务器数据可能保留。");
        lv_label_set_text(footer,s.delete_commit_busy?"请稍候":s.delete_review_busy?"长按 OK 取消":
                          s.delete_review.stage==MEETING_DELETE_UI_CONFIRM?"OK 删除 | 长按取消":"OK 继续 | 长按取消");return;
    }
    if(wifi){
        lv_label_set_text(title,"导出文件");
        lv_label_set_text_fmt(body,"网络:\n%s\n\n密码:\n%s\n\n文件数: %lu",s.ssid,
                              s.password[0]?s.password:"无密码",(unsigned long)s.wifi_files);
        lv_label_set_text(footer,"长按 OK 停止");return;
    }
    if(page==0){
        lv_label_set_text(title,"会议录音");
        refresh_recording_panel(&s);
        const char *detail=upload_detail_text(s.network);
        lv_label_set_text_fmt(body,"录音 %lu 条 | 剩余 %lu KB%s%s",
            (unsigned long)s.file_count,(unsigned long)s.free_kb,detail[0]?"\n":"",detail);
        lv_label_set_text(footer,s.toast[0]?sc_ui_toast_text(s.toast):
                          s.rec_status==SC_REC_RECORDING?"OK 暂停 | 长按保存":
                          s.rec_status==SC_REC_PAUSED?"OK 继续 | 长按保存":
                          s.continuation_upload?"上传后续录 | 长按取消":"OK 录音 | 长按保存");
    }else if(page==1){
        static sc_file_entry_t files[ROWS];char text[256];size_t pos=0;
        int selected=atomic_load(&selected_index);size_t start=selected<0?0:(size_t)selected/ROWS*ROWS;
        size_t count=sc_core_files_range(start,files,ROWS);
        uint32_t displayed=0;
        lv_label_set_text(title,s.timezone_known?"录音列表":"录音列表 UTC");
        if(display_start!=start){memset(display_times,0,sizeof(display_times));display_start=start;}
        if(!count)snprintf(text,sizeof(text),"暂无录音。");
        else for(size_t i=0;i<count&&pos<sizeof(text);i++){
            if(selected>=0&&(size_t)selected==start+i)displayed=files[i].file_id;
            uint32_t sec=files[i].size/SC_UNIT_DATA/50u;
            display_time *stamp=&display_times[i];
            if(stamp->id!=files[i].file_id){
                bool present=false;int64_t epoch=0;uint8_t trust=0;
                stamp->id=files[i].file_id;
                stamp->known=sc_store_record_time(stamp->id,&present,&epoch,&trust)&&present&&trust==2;
                stamp->epoch=epoch;
            }
            char date[24];time_t seconds=(time_t)(stamp->epoch+(s.timezone_known?(int64_t)s.timezone_minutes*60:0));
            struct tm date_tm;
            if(!(stamp->known&&gmtime_r(&seconds,&date_tm)&&
                 strftime(date,sizeof(date),"%y-%m-%d %H:%M",&date_tm)))
                snprintf(date,sizeof(date),"#%lu 时间未知",(unsigned long)files[i].file_id);
            int n=snprintf(text+pos,sizeof(text)-pos,"%s%s %lu:%02lu\n",
                selected>=0&&(size_t)selected==start+i?"> ":"  ",date,
                (unsigned long)(sec/60),(unsigned long)(sec%60));
            if(n<0||(size_t)n>=sizeof(text)-pos)break;
            pos+=(size_t)n;
        }
        lv_label_set_text(body,text);atomic_store(&displayed_selection_id,displayed);
        lv_label_set_text(footer,s.toast[0]?sc_ui_toast_text(s.toast):(selected<0?"OK 选择 | 上下翻页":"上下选择 | 长按删除"));
    }else{
        lv_label_set_text(title,"配置");
        char zone[24]="";
        if(s.timezone_known){int minutes=s.timezone_minutes;char sign=minutes<0?'-':'+';if(minutes<0)minutes=-minutes;
            snprintf(zone,sizeof(zone)," (UTC%c%02d:%02d)",sign,minutes/60,minutes%60);}
        const char *wifi_state=s.wifi_has_ip?"已连接":wifi_connecting?"连接中":
                               s.wifi_ap_missing?"未找到":"未连接";
        const char *ble_state=!s.config_ble_active?"未启用":s.config_ble_authenticated?"已连接":
                              s.config_ble_connected?"连接中":"等待连接";
        char *ssid=s.connected_ssid[0]?s.connected_ssid:s.target_ssid;
        for(char *c=ssid;*c;c++){
            if((unsigned char)*c<32||*c==127)*c=' ';
        }
        lv_label_set_text_fmt(config_wifi_row,"Wi-Fi: %s",wifi_state);
        /* Replacing the label text on every refresh restarts its scroll. */
        if(strcmp(lv_label_get_text(config_wifi_name),ssid))
            lv_label_set_text(config_wifi_name,ssid);
        lv_obj_remove_flag(config_wifi_row,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(config_wifi_name,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(config_ble_row,"蓝牙: %s",ble_state);
        if(strcmp(lv_label_get_text(config_ble_name),s.config_peer_name))
            lv_label_set_text(config_ble_name,s.config_peer_name);
        lv_obj_remove_flag(config_ble_row,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(config_ble_name,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(body,95+2*(meeting_font_16.line_height+5));
        lv_label_set_text_fmt(body,"\n设备编号\n%s\n时间: %s%s\n可用空间: %lu KB",
                              s.serial,
                              s.time_synced?"已同步":s.time_checking?"检查中":"未同步",zone,(unsigned long)s.free_kb);
        lv_label_set_text(footer,s.toast[0]?sc_ui_toast_text(s.toast):s.config_ble_active?"长按 OK 取消":"长按 OK 启用蓝牙配置");
    }
}

static void create_recording_panel(lv_obj_t *screen)
{
    recording_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(recording_panel);
    lv_obj_set_pos(recording_panel, 0, 78);
    lv_obj_set_size(recording_panel, lv_pct(100), RING_DIAMETER);
    lv_obj_remove_flag(recording_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    recording_ring = lv_arc_create(recording_panel);
    lv_obj_remove_style_all(recording_ring);
    lv_obj_remove_flag(recording_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(recording_ring, RING_DIAMETER, RING_DIAMETER);
    lv_obj_center(recording_ring);
    lv_arc_set_bg_angles(recording_ring, 0, 360);
    lv_arc_set_rotation(recording_ring, 270);
    lv_arc_set_range(recording_ring, 0, 100);
    lv_obj_set_style_arc_width(recording_ring, RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(recording_ring, lv_color_hex(RING_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(recording_ring, RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(recording_ring, lv_color_hex(BLUE), LV_PART_INDICATOR);
    lv_arc_set_value(recording_ring, 0);

    recording_clock = make_text(recording_panel, &lv_font_montserrat_32, 0,
                                (RING_DIAMETER - lv_font_montserrat_32.line_height) / 2, lv_pct(100), INK);
    lv_obj_set_style_text_align(recording_clock, LV_TEXT_ALIGN_CENTER, 0);
    recording_state = make_text(recording_panel, &meeting_font_16, 0, RING_DIAMETER / 2 + 25, lv_pct(100), MUTED);
    lv_obj_set_style_text_align(recording_state, LV_TEXT_ALIGN_CENTER, 0);
}

esp_err_t sc_ui_init(void)
{
    lv_obj_t *screen=lv_obj_create(NULL);lv_obj_set_scrollable(screen,false);
    lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    network_icon=make_text(screen,LV_FONT_DEFAULT,20,20,19,BLUE);
    static const lv_point_precise_t slash_points[]={{0,15},{16,0}};
    wifi_slash=lv_line_create(screen);
    lv_line_set_points(wifi_slash,slash_points,2);
    lv_obj_set_pos(wifi_slash,20,21);
    lv_obj_set_style_line_width(wifi_slash,2,0);
    lv_obj_set_style_line_color(wifi_slash,lv_color_hex(BLUE),0);

    lv_obj_t *battery_frame=lv_obj_create(screen);
    lv_obj_set_pos(battery_frame,195,22);lv_obj_set_size(battery_frame,22,12);
    lv_obj_set_scrollable(battery_frame,false);
    lv_obj_set_style_bg_opa(battery_frame,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_color(battery_frame,lv_color_hex(BLUE),0);
    lv_obj_set_style_border_width(battery_frame,1,0);
    lv_obj_set_style_radius(battery_frame,2,0);
    lv_obj_t *battery_tip=lv_obj_create(screen);
    lv_obj_set_pos(battery_tip,217,26);lv_obj_set_size(battery_tip,3,4);
    lv_obj_set_scrollable(battery_tip,false);
    lv_obj_set_style_bg_color(battery_tip,lv_color_hex(BLUE),0);
    lv_obj_set_style_bg_opa(battery_tip,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(battery_tip,0,0);lv_obj_set_style_radius(battery_tip,1,0);
    battery_fill=lv_obj_create(screen);
    lv_obj_set_pos(battery_fill,197,24);lv_obj_set_size(battery_fill,18,8);
    lv_obj_set_scrollable(battery_fill,false);
    lv_obj_set_style_bg_color(battery_fill,lv_color_hex(BLUE),0);
    lv_obj_set_style_bg_opa(battery_fill,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(battery_fill,0,0);lv_obj_set_style_radius(battery_fill,0,0);
    battery_unknown=make_text(screen,&meeting_font_16,201,20,12,BLUE);
    lv_label_set_text(battery_unknown,"?");
    title=make_text(screen,&meeting_font_16,20,55,200,INK);
    body=make_text(screen,&meeting_font_16,20,95,200,INK);
    create_recording_panel(screen);
    config_wifi_row=make_text(screen,&meeting_font_16,20,95,200,INK);
    config_wifi_name=make_text(screen,&meeting_font_16,116,95,104,INK);
    lv_label_set_long_mode(config_wifi_name,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    config_ble_row=make_text(screen,&meeting_font_16,20,95+meeting_font_16.line_height+5,200,INK);
    config_ble_name=make_text(screen,&meeting_font_16,116,95+meeting_font_16.line_height+5,104,INK);
    lv_label_set_long_mode(config_ble_name,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    footer=make_text(screen,&meeting_font_16,20,282,200,MUTED);
    lv_screen_load(screen);
    refresh(NULL);
    lv_timer_create(refresh, 200, NULL);
    lv_timer_create(refresh_recording_level, 80, NULL);
    return ESP_OK;
}
void sc_ui_page_step(int delta)
{
    int page=atomic_load(&page_request);page=(page+delta+3)%3;atomic_store(&page_request,page);
}
bool sc_ui_device_page(void){return atomic_load(&page_request)==2;}
bool sc_ui_wifi_page(void){return atomic_load(&keep_awake);}
bool sc_ui_handle_key(bsp_btn_t btn,bsp_btn_ev_t event)
{
    if(event!=BSP_BTN_CLICK&&event!=BSP_BTN_LONG)return false;
    sc_snapshot_t s;sc_core_snapshot(&s);
    if(s.setup.state!=MEETING_SETUP_IDLE||!s.storage_ready||s.link>=SC_LINK_WIFI_PENDING)return false;
    if(s.delete_review_busy||s.delete_review.stage!=MEETING_DELETE_UI_IDLE){
        if(btn==BSP_BTN_OK&&event==BSP_BTN_LONG&&!s.delete_commit_busy)(void)sc_core_cancel_delete_review();
        else if(btn==BSP_BTN_OK&&event==BSP_BTN_CLICK&&!s.delete_review_busy)(void)sc_core_confirm_delete_review();
        return true;
    }
    if(atomic_load(&page_request)!=1||s.rec_status!=SC_REC_STOPPED)return false;
    int selected=atomic_load(&selected_index);
    if(selected<0){
        if(btn==BSP_BTN_OK&&event==BSP_BTN_CLICK){if(s.file_count){atomic_store(&displayed_selection_id,0);atomic_store(&selected_index,0);}return true;}
        return false;
    }
    if(!s.file_count){atomic_store(&selected_index,-1);return true;}
    if((uint32_t)selected>=s.file_count)selected=(int)s.file_count-1;
    if(event==BSP_BTN_CLICK&&(btn==BSP_BTN_UP||btn==BSP_BTN_DOWN)){
        int count=(int)s.file_count;selected=(selected+(btn==BSP_BTN_UP?-1:1)+count)%count;atomic_store(&displayed_selection_id,0);atomic_store(&selected_index,selected);return true;
    }
    if(btn==BSP_BTN_OK&&event==BSP_BTN_CLICK){atomic_store(&displayed_selection_id,0);atomic_store(&selected_index,-1);return true;}
    if(btn==BSP_BTN_OK&&event==BSP_BTN_LONG){
        sc_file_entry_t file;uint32_t displayed=atomic_load(&displayed_selection_id);
        if(displayed&&sc_core_files_range((size_t)selected,&file,1)==1&&file.file_id==displayed)
            (void)sc_core_review_delete(file.file_id);
        return true;
    }
    return true;
}
