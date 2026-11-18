#include "meeting_config_ble.h"
#include <assert.h>
#include <stdlib.h>

static int held,valid=1;
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &held;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock,unsigned wait){(void)wait;if(*(int *)lock)return 0;*(int *)lock=1;return pdTRUE;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock){*(int *)lock=0;return pdTRUE;}
void vSemaphoreDelete(SemaphoreHandle_t lock){(void)lock;}
bool meeting_pairing_ble_capture(uint32_t session,meeting_pairing_token *token)
{if(!valid)return false;token->generation=1;token->session=session;return true;}
bool meeting_pairing_ble_session_valid(meeting_pairing_token token)
{return valid&&token.generation==1&&token.session==7;}
static unsigned request(meeting_config_ble_inbox *inbox,const uint8_t *data,size_t size)
{
    uint8_t *response=NULL;ssize_t length=0;
    assert(meeting_config_ble_handle(7,data,size,&response,&length,inbox)==ESP_OK);
    assert(length==1);unsigned result=response[0];free(response);return result;
}
int main(void)
{
    meeting_config_ble_inbox small;assert(meeting_config_ble_init_sized(&small,118));
    assert(small.fragments.capacity==118);
    const uint8_t oversized[]={1,119,0};assert(request(&small,oversized,sizeof oversized)==1);
    meeting_config_ble_destroy(&small);
    meeting_config_ble_inbox inbox;assert(meeting_config_ble_init(&inbox));
    const uint8_t start[]={1,1,0},chunk[]={2,0,0,'x'},end[]={3};
    uint8_t copy[4];size_t length;meeting_pairing_token token;
    assert(request(&inbox,start,sizeof start)==0);
    assert(request(&inbox,chunk,sizeof chunk)==0);
    assert(request(&inbox,end,sizeof end)==0);
    const uint8_t status[]={4},ack[]={5};
    assert(request(&inbox,status,sizeof status)==0); /* consumer has not run yet */
    assert(request(&inbox,ack,sizeof ack)==1);
    assert(meeting_config_ble_take(&inbox,copy,sizeof copy,&length,&token));
    assert(length==1&&copy[0]=='x'&&token.session==7);
    assert(inbox.fragments.bytes[0]==0);
    assert(!meeting_config_ble_take(&inbox,copy,sizeof copy,&length,&token));
    assert(request(&inbox,start,sizeof start)==1);
    assert(request(&inbox,status,sizeof status)==0);
    assert(request(&inbox,ack,sizeof ack)==1);
    assert(meeting_config_ble_publish(&inbox,token));
    assert(request(&inbox,status,sizeof status)==2);
    assert(request(&inbox,ack,sizeof ack)==0);
    assert(meeting_config_ble_acknowledged(&inbox,token));
    meeting_config_ble_reset(&inbox);
    assert(meeting_config_ble_acknowledged(&inbox,token)); /* acknowledgement survives disconnect */
    meeting_config_ble_prepare(&inbox);
    assert(!meeting_config_ble_acknowledged(&inbox,token));
    assert(request(&inbox,start,sizeof start)==0);
    assert(request(&inbox,chunk,sizeof chunk)==0);
    assert(request(&inbox,end,sizeof end)==0);
    valid=0;
    assert(!meeting_config_ble_take(&inbox,copy,sizeof copy,&length,&token));
    valid=1;
    held=1;meeting_config_ble_reset(&inbox);held=0;
    assert(!meeting_config_ble_take(&inbox,copy,sizeof copy,&length,&token));
    assert(!inbox.fragments.sealed);
    assert(request(&inbox,start,sizeof start)==0);
    assert(request(&inbox,chunk,sizeof chunk)==0);
    assert(request(&inbox,end,sizeof end)==0);
    assert(meeting_config_ble_take(&inbox,copy,sizeof copy,&length,&token));
    assert(!meeting_config_ble_publish_result(&inbox,token,3));
    assert(meeting_config_ble_publish_result(&inbox,token,19));
    assert(meeting_config_ble_publish_result(&inbox,token,19));
    assert(!meeting_config_ble_publish_result(&inbox,token,20));
    assert(request(&inbox,status,sizeof status)==19);
    assert(request(&inbox,ack,sizeof ack)==0);
    meeting_config_ble_reset(&inbox);
    assert(request(&inbox,start,sizeof start)==0);
    held=1;assert(request(&inbox,chunk,sizeof chunk)==1);held=0;
    meeting_config_ble_destroy(&inbox);
    return 0;
}
