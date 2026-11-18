#include "meeting_management_ble.h"
#include <string.h>
esp_err_t meeting_management_ble_start(const meeting_mgmt_io *io,const uint8_t expected_id[16],
                                       meeting_pairing_handler handler,meeting_pairing_reset reset,void *ctx) {
    if(!io||!expected_id||!handler||!reset)return ESP_ERR_INVALID_ARG;
    meeting_mgmt_record record={0};
    meeting_mgmt_result status=meeting_mgmt_load(io,&record);
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if(status==MEETING_MGMT_OK&&record.state==MEETING_MGMT_ACTIVE&&
       !memcmp(record.device_id,expected_id,16))
        result=meeting_pairing_ble_start_management(record.secret,handler,reset,ctx);
    meeting_mgmt_clear(&record);
    return result;
}
