/**--------------He thong quan ly diem danh sinh vien--------------**/
/**-------------- Mai Tung --------------**/
/**--------------So do noi chan--------------**/

/**		STM32F103	 --------------		RFID RC522	**/
/**		3.3V	 		 --------------		3.3V				**/
/**		GND     	 --------------		GND					**/
/**		A4	     	 --------------		SDA					**/
/**		A5	     	 --------------		SCK					**/
/**		A7	     	 --------------		MOSI				**/
/**		A6	     	 --------------		MISO				**/
/**		A3	     	 --------------		RST					**/


/**		STM32F103	 --------------		SSD1306			**/
/**		3.3V	 		 --------------		VCC					**/
/**		GND     	 --------------		GND					**/
/**		B6	     	 --------------		SCL					**/
/**		B7	     	 --------------		SDA					**/

/**		STM32F103	 --------------		BUTTON									**/
/**		B0	 		   --------------		CHINH MODE							**/
/**		B1     	   --------------		TANG THOI GIAN					**/
/**		B10	     	 --------------		GIAM THOI GIAN					**/
/**		B13	     	 --------------		CHON CAC MODE THOI GIAN **/

#include "stm32f10x.h"
#include "stm32f10x_spi.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "misc.h"
#include "rc522.h"
#include <stdio.h>
#include <string.h>

#include "ssd1306.h"
#include "fonts.h"


#define START_YEAR       2025
#define START_MONTH      10
#define START_DAY        3
#define START_HOUR       5
#define START_MIN        6
#define START_SEC        0

#define FLASH_PAGE_SIZE          ((uint16_t)0x400)
#define ATTENDANCE_START_ADDR    ((uint32_t)0x0800FC00)
#define ATTENDANCE_END_ADDR      ((uint32_t)0x0800FFFF)


#define BUTTON_GPIO_PORT         GPIOB
#define BUTTON_GPIO_PIN          GPIO_Pin_0

#define BUTTON_UP_GPIO_PORT      GPIOB
#define BUTTON_UP_GPIO_PIN       GPIO_Pin_1

#define BUTTON_DOWN_GPIO_PORT    GPIOB  
#define BUTTON_DOWN_GPIO_PIN     GPIO_Pin_10

#define BUTTON_SELECT_GPIO_PORT  GPIOB
#define BUTTON_SELECT_GPIO_PIN   GPIO_Pin_13

typedef struct {
    uint32_t timestamp;       
    uint8_t studentIndex;
    uint8_t status;
    uint8_t checksum;
} AttendanceRecord;

typedef struct {
    uint8_t uid[5];
    char mssv[100];
    char name[30];
} StudentData;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
} DateTime;


StudentData registeredStudents[] = {
    {{0x77,0xA1,0x30,0xC2,0x24}, "DT060150", "Thanh Tung"},
    {{0x17,0x6B,0x08,0xC2,0xB6}, "DT060148", "Viet Tri"},
    {{0x64,0xD0,0xF8,0x04,0x48}, "DT060113", "Van Khang"},
    {{0x47,0x2D,0x23,0xC2,0x8B}, "DT060215", "Trung Kien"},
};

uint8_t studentCount = sizeof(registeredStudents) / sizeof(registeredStudents[0]);
uint8_t attendanceList[50] = {0};
uint32_t recordCount = 0;

volatile uint32_t msTicks = 0;  // B? d?m mili giây

uint8_t configMode = 0;  // 0: Che do doc RFID,  1: Che do chinh thoi gian


typedef enum {
    ADJUST_HOUR,
    ADJUST_MINUTE,
    ADJUST_SECOND,
    ADJUST_DAY,
    ADJUST_MONTH,
    ADJUST_YEAR,
    ADJUST_NONE
} AdjustField_t;

volatile AdjustField_t currentAdjustField = ADJUST_HOUR;
volatile int32_t timeAdjustment = 0; 

char buffer[100];

/**
 * @brief Chuyen doi mili giay ke tu khi he thong bat dau sang cau truc DateTime dua tren macro bat dau da dinh nghia.
          Xu ly tran cho giay, phut, gio, ngay (gia su 30 ngay/thang, 365 ngay/nam).

 * @param[in] millis Tong so mili giay can chuyen doi.

 * @param[out] dt Con tro den cau truc DateTime de luu ket qua.

 * @return Khong co.
 */


void ConvertMillisToDateTime(uint32_t millis, DateTime *dt) {
    uint32_t totalSeconds = millis / 1000;
    
   
    dt->sec = (totalSeconds % 60) + START_SEC;
    uint32_t totalMinutes = totalSeconds / 60;
    dt->min = (totalMinutes % 60) + START_MIN;
    uint32_t totalHours = totalMinutes / 60;
    dt->hour = (totalHours % 24) + START_HOUR;
    
   
    uint32_t totalDays = totalHours / 24;
    
    
    dt->day = START_DAY;
    dt->month = START_MONTH;
    dt->year = START_YEAR;
    
    
    for(uint32_t i = 0; i < totalDays; i++) {
        dt->day++;
        
        
        if(dt->day > 30) {
            dt->day = 1;
            dt->month++;
            
            
            if(dt->month > 12) {
                dt->month = 1;
                dt->year++;
            }
        }
    }
    
    
    if(dt->sec >= 60) {
        dt->sec -= 60;
        dt->min++;
    }
    if(dt->min >= 60) {
        dt->min -= 60;
        dt->hour++;
    }
    if(dt->hour >= 24) {
        dt->hour -= 24;
        dt->day++;
        
        
        if(dt->day > 30) {
            dt->day = 1;
            dt->month++;
            
            if(dt->month > 12) {
                dt->month = 1;
                dt->year++;
            }
        }
    }
}


void USART1_Config(void){
    USART_InitTypeDef USART_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_Cmd(USART1, ENABLE);
}


void SPI1_Config(void){
    SPI_InitTypeDef SPI_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
}


void TIM2_Init(void){
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Period = 1000 - 1; // 1ms
    TIM_TimeBaseStructure.TIM_Prescaler = (SystemCoreClock / 1000000) - 1; // Clock 1MHz -> tick 1us
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void){
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET){
        msTicks++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

void USART1_SendChar(char c){
    while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, c);
}

void USART1_SendString(char *str){
    while(*str){
        USART1_SendChar(*str++);
    }
}

void FLASH_Config(void){
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
}

void FLASH_ERASEPAGE(void){
    FLASH_ErasePage(ATTENDANCE_START_ADDR);
    recordCount = 0;
    for(uint8_t i = 0; i < studentCount; i++){
        attendanceList[i] = 0;
    }
    USART1_SendString("Da xoa lich su diem danh!\r\n");
}


/**
 * @brief Tinh tong kiem XOR cho tinh lien ket du lieu.

 * @param data Con tro den mang du lieu.

 * @param length Do dai du lieu tinh bang byte.

 * @return uint8_t Gia tri tong kiem.
 */


uint8_t CalculateChecksum(uint8_t *data, uint8_t length){
    uint8_t checksum = 0;
    for(uint8_t i = 0; i < length; i++){
        checksum ^= data[i];
    }
    return checksum;
}


/**
 * @brief Ghi AttendanceRecord vao Flash tai dia chi tiep theo; xoa trang neu day.

 * @param[in] record Con tro den ban ghi can ghi.

 * @return Khong co.
 */

void FLASH_WriteRecord(AttendanceRecord *record){
    uint32_t address = ATTENDANCE_START_ADDR + (recordCount * sizeof(AttendanceRecord));
    if(address + sizeof(AttendanceRecord) >= ATTENDANCE_END_ADDR){
        USART1_SendString("Bo nho day! Xoa du lieu cu...\r\n");
        FLASH_ERASEPAGE();
        address = ATTENDANCE_START_ADDR;
    }
    record->checksum = CalculateChecksum((uint8_t*)record, sizeof(AttendanceRecord) - 1);
    uint32_t *pData = (uint32_t*)record;
    for(uint8_t i = 0; i < sizeof(AttendanceRecord)/4; i++){
        FLASH_ProgramWord(address + (i * 4), pData[i]);
    }
    recordCount++;
}


/**
 * @brief Doc ban ghi diem danh tu Flash, kiem tra tong kiem, va cap nhat attendanceList.

 * @param Khong co.

 * @return Khong co.
 */

void FLASH_ReadRecords(void){
    uint32_t address = ATTENDANCE_START_ADDR;
    AttendanceRecord record;
    uint8_t validRecords = 0;
    for(uint8_t i=0; i<studentCount; i++){
        attendanceList[i] = 0;
    }
    while(address < ATTENDANCE_END_ADDR){
        uint32_t *pRecord = (uint32_t*)&record;
        for(uint8_t i=0; i<(sizeof(AttendanceRecord)/4); i++){
            pRecord[i] = *(__IO uint32_t*)(address + (i*4));
        }
        if(pRecord[0] == 0xFFFFFFFF) break;
        uint8_t cs = CalculateChecksum((uint8_t*)&record, sizeof(AttendanceRecord) - 1);
        if(record.checksum == cs && record.studentIndex < studentCount){
            attendanceList[record.studentIndex] = record.status;
            validRecords++;
            address += sizeof(AttendanceRecord);
        } else {
            break;
        }
    }
    recordCount = validRecords;
    USART1_SendString("Da doc ban ghi diem danh tu Flash\r\n");
}


/**
 * @brief So sanh hai mang UID de kiem tra bang nhau.

 * @param[in] uid1 Mang UID dau tien.

 * @param[in] uid2 Mang UID thu hai.

 * @param[in] length Do dai UID can so sanh.

 * @return uint8_t 1 neu khop, 0 neu khong.
 */


uint8_t UID_Match(uint8_t *uid1, uint8_t *uid2, uint8_t length){
    for(uint8_t i=0;i<length;i++){
        if(uid1[i] != uid2[i]) return 0;
    }
    return 1;
}


/**
 * @brief Kiem tra UID co khop voi UID sinh vien da dang ky; 
					Tra ve chi so hoac -1.

 * @param uid UID can kiem tra.

 * @param uidLength Do dai UID.

 * @return int8_t Chi so sinh vien neu da dang ky, -1 neu khong.
 */

int8_t CheckCardRegistered(uint8_t *uid, uint8_t uidLength){
    for(uint8_t i=0; i<studentCount; i++){
        if(UID_Match(uid, registeredStudents[i].uid, uidLength)) return i;
    }
    return -1;
}


/**
 * @brief Hien thi thong tin sinh vien (MSSV va ten) qua USART1.

 * @param index Chi so sinh vien trong registeredStudents.

 * @return Khong co.
 */

void DisplayStudentInfo(uint8_t index){
    sprintf(buffer, "MSSV: %s | Ten: %s\r\n",
        registeredStudents[index].mssv,
        registeredStudents[index].name);
    USART1_SendString(buffer);
}

/**
 * @brief Xu ly diem danh: Kiem tra dang ky, lap lai, va khung gio; 
					Ghi log va luu neu hop le.


 * @param[in] studentIndex Chi so sinh vien (-1 neu chua dang ky).

 * @return Khong co.
 */

void MarkAttendance(int8_t studentIndex){
    if(studentIndex < 0){
        USART1_SendString("THE CHUA DANG KY!\r\n");
        USART1_SendString("LIEN HE LAI GIAO VIEN DE DANG KY THE.\r\n");
        USART1_SendString("----------\r\n");
        return;
    }
    if(attendanceList[studentIndex] == 1){
        USART1_SendString("----------DA DIEM DANH TRUOC DO----------\r\n");
        DisplayStudentInfo(studentIndex);
        USART1_SendString("----------\r\n");
        return;
    }

    DateTime dt;
    ConvertMillisToDateTime(msTicks, &dt);

    int currentMinutes = dt.hour * 60 + dt.min;
    int startMinutes = START_HOUR * 60 + START_MIN;

    if(currentMinutes < startMinutes){
        USART1_SendString("CHUA DEN GIO DIEM DANH!\r\n");
        return;
    }
    else if(currentMinutes < startMinutes + 2){
        USART1_SendString("DIEM DANH DUNG GIO\r\n");
    }
    else if(currentMinutes < startMinutes + 4){
        USART1_SendString("DIEM DANH MUON\r\n");
    }
    else{
        USART1_SendString("DA KHOA DIEM DANH, TINH LA VANG\r\n");
        return;
    }

    AttendanceRecord record;
    record.timestamp = msTicks;
    record.studentIndex = studentIndex;
    record.status = 1;

    attendanceList[studentIndex] = 1;

    USART1_SendString("----------DIEM DANH THANH CONG----------\r\n");
    DisplayStudentInfo(studentIndex);

    sprintf(buffer, "Ngay: %04d-%02d-%02d %02d:%02d:%02d\r\n",
        dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
    USART1_SendString(buffer);
    USART1_SendString("----------\r\n");

    FLASH_WriteRecord(&record);
}


void Button_GPIO_Config(void){
    GPIO_InitTypeDef GPIO_InitStructure;
    
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
   
    GPIO_InitStructure.GPIO_Pin = BUTTON_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BUTTON_GPIO_PORT, &GPIO_InitStructure);
    
    
    GPIO_InitStructure.GPIO_Pin = BUTTON_UP_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BUTTON_UP_GPIO_PORT, &GPIO_InitStructure);
    
     
    GPIO_InitStructure.GPIO_Pin = BUTTON_DOWN_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BUTTON_DOWN_GPIO_PORT, &GPIO_InitStructure);
    
    
    GPIO_InitStructure.GPIO_Pin = BUTTON_SELECT_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BUTTON_SELECT_GPIO_PORT, &GPIO_InitStructure);
}

/**
 * @brief Kiem tra nut chinh (PB0) co duoc nhan khong (active low).

 * @param Khong co.

 * @return uint8_t 1 neu nhan, 0 neu khong.
 */

uint8_t IsButtonPressed(void){
    return GPIO_ReadInputDataBit(BUTTON_GPIO_PORT, BUTTON_GPIO_PIN) == 0;
}

/**
 * @brief Kiem tra nut chinh (PB1) co duoc nhan khong (active low).

 * @param Khong co.

 * @return uint8_t 1 neu nhan, 0 neu khong.
 */

uint8_t IsButtonUpPressed(void){
    return GPIO_ReadInputDataBit(BUTTON_UP_GPIO_PORT, BUTTON_UP_GPIO_PIN) == 0;
}

/**
 * @brief Kiem tra nut chinh (PB10) co duoc nhan khong (active low).

 * @param Khong co.

 * @return uint8_t 1 neu nhan, 0 neu khong.
 */

uint8_t IsButtonDownPressed(void){
    return GPIO_ReadInputDataBit(BUTTON_DOWN_GPIO_PORT, BUTTON_DOWN_GPIO_PIN) == 0;
}

/**
 * @brief Kiem tra nut chinh (PB13) co duoc nhan khong (active low).

 * @param Khong co.

 * @return uint8_t 1 neu nhan, 0 neu khong.
 */

uint8_t IsButtonSelectPressed(void){
    return GPIO_ReadInputDataBit(BUTTON_SELECT_GPIO_PORT, BUTTON_SELECT_GPIO_PIN) == 0;
}


/**
 * @brief Ap dung dieu chinh thoi gian dang cho vao msTicks (chuyen giay sang ms).

 * @param Khong co.

 * @return Khong co.
 */

void ApplyTimeAdjustment(void) {
    if (timeAdjustment != 0) {
        msTicks += (timeAdjustment * 1000);
        timeAdjustment = 0; 
    }
}


/**
 * @brief Xu ly dau vao nut trong che do cau hinh: Chuyen truong va dieu chinh thoi gian (loai bo nhieu).
 
 * @param Khong co.

 * @return Khong co.
 */

void HandleTimeAdjustment(void) {
    static uint8_t lastSelectState = 1;
    static uint8_t lastUpState = 1;
    static uint8_t lastDownState = 1;
    
    uint8_t currentSelectState = IsButtonSelectPressed();
    uint8_t currentUpState = IsButtonUpPressed();
    uint8_t currentDownState = IsButtonDownPressed();
    
    
    if (currentSelectState && !lastSelectState) {
        currentAdjustField = (currentAdjustField + 1) % 6;
    }
    
    
    if (currentUpState && !lastUpState) {
        switch (currentAdjustField) {
            case ADJUST_HOUR:
                timeAdjustment += 3600; 
                break;
            case ADJUST_MINUTE:
                timeAdjustment += 60;  
                break;
            case ADJUST_SECOND:
                timeAdjustment += 1;   
                break;
            case ADJUST_DAY:
                timeAdjustment += 86400; 
                break;
            case ADJUST_MONTH:
            
                timeAdjustment += 2592000; 
                break;
            case ADJUST_YEAR:
               
                timeAdjustment += 31536000; 
                break;
            default:
                break;
        }
        ApplyTimeAdjustment();
    }
    

    if (currentDownState && !lastDownState) {
        switch (currentAdjustField) {
            case ADJUST_HOUR:
                timeAdjustment -= 3600;
                break;
            case ADJUST_MINUTE:
                timeAdjustment -= 60;
                break;
            case ADJUST_SECOND:
                timeAdjustment -= 1;
                break;
            case ADJUST_DAY:
                timeAdjustment -= 86400;
                break;
            case ADJUST_MONTH:
                timeAdjustment -= 2592000;
                break;
            case ADJUST_YEAR:
                timeAdjustment -= 31536000;
                break;
            default:
                break;
        }
        ApplyTimeAdjustment();
    }
    

    lastSelectState = currentSelectState;
    lastUpState = currentUpState;
    lastDownState = currentDownState;
}


/**
 * @brief Hien thi tong ket diem danh tren OLED (ten sinh vien va trang thai: Co/Vang).

 * @param Khong co.

 * @return Khong co.
 */

void OLED_ShowAttendanceSummary(void){
    SSD1306_Clear();

    SSD1306_GotoXY(3,0);
    SSD1306_Puts("Diem danh:", &Font_7x10, SSD1306_COLOR_WHITE);

    for(uint8_t i = 0; i < studentCount; i++){
        SSD1306_GotoXY(3, 12 + i*10);

        char line[32];
        snprintf(line, sizeof(line), "%s %s", registeredStudents[i].name,
            attendanceList[i] ? "Co" : "Vang");

        SSD1306_Puts(line, &Font_7x10, SSD1306_COLOR_WHITE);

        if(i >= 5) break; 
    }

    SSD1306_UpdateScreen();
}

/**
 * @brief Hien thi thoi gian hien tai tren OLED cho che do chinh, voi chi bao truong va huong dan nut.

 * @param Khong co.

 * @return Khong co.
 */

void OLED_ShowCurrentTime(void){
    DateTime dt;
    ConvertMillisToDateTime(msTicks, &dt);

    SSD1306_Clear();
    SSD1306_GotoXY(0,0);
    SSD1306_Puts("Chinh thoi gian", &Font_7x10, SSD1306_COLOR_WHITE);

    char timeStr[32];
    char fieldStr[20];
    
    
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d", dt.year, dt.month, dt.day);
    SSD1306_GotoXY(0, 15);
    
  
    switch(currentAdjustField) {
        case ADJUST_YEAR: strcpy(fieldStr, "Year  <"); break;
        case ADJUST_MONTH: strcpy(fieldStr, "Month <"); break;
        case ADJUST_DAY: strcpy(fieldStr, "Day   <"); break;
        default: strcpy(fieldStr, ""); break;
    }
    
    SSD1306_Puts(timeStr, &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_GotoXY(80, 15);
    SSD1306_Puts(fieldStr, &Font_7x10, SSD1306_COLOR_WHITE);

  
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", dt.hour, dt.min, dt.sec);
    SSD1306_GotoXY(0, 30);
    
   
    switch(currentAdjustField) {
        case ADJUST_HOUR: strcpy(fieldStr, "Hour  <"); break;
        case ADJUST_MINUTE: strcpy(fieldStr, "Minute<"); break;
        case ADJUST_SECOND: strcpy(fieldStr, "Second<"); break;
        default: strcpy(fieldStr, ""); break;
    }
    
    SSD1306_Puts(timeStr, &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_GotoXY(80, 30);
    SSD1306_Puts(fieldStr, &Font_7x10, SSD1306_COLOR_WHITE);

 
    SSD1306_GotoXY(0, 50);
    SSD1306_Puts("B1:Tang", &Font_7x10, SSD1306_COLOR_WHITE);
    
    SSD1306_GotoXY(50, 50);
    SSD1306_Puts("B3:Giam", &Font_7x10, SSD1306_COLOR_WHITE);
    
    SSD1306_GotoXY(0, 60);
    SSD1306_Puts("B13:Chon", &Font_7x10, SSD1306_COLOR_WHITE);

    SSD1306_UpdateScreen();
}

/**
 * @brief Khoi tao he thong diem danh: In thong bao chan don va tai ban ghi tu Flash.

 * @param Khong co.

 * @return Khong co.
 */

void AttendanceSystem_Config(void){
    USART1_SendString("HE THONG DIEM DANH SINH VIEN\r\n");
    USART1_SendString("San sang doc the RFID...\r\n");
    USART1_SendString("============================\r\n");
    FLASH_Config();
    FLASH_ReadRecords();
}

int main(void){
    uint8_t uid[10];
    uint8_t uidLength;
    int8_t studentIndex;


    SPI1_Config();
    USART1_Config();
    RC522_Config();
    TIM2_Init();
    Button_GPIO_Config(); 

    SSD1306_Config();
    AttendanceSystem_Config();

    OLED_ShowAttendanceSummary();

    while(1){
        
        if(IsButtonPressed()){
            while(IsButtonPressed());
            configMode = !configMode;

            if(configMode){
                currentAdjustField = ADJUST_HOUR; 
                OLED_ShowCurrentTime();
            } else {
                OLED_ShowAttendanceSummary();
            }
        }

        if(configMode){
 
            HandleTimeAdjustment();
            OLED_ShowCurrentTime();
            
            
            for(volatile uint32_t j=0; j<100000; j++);
        } else {
       
            if(RC522_Check(uid, &uidLength) == MI_OK){
                studentIndex = CheckCardRegistered(uid, uidLength);
                MarkAttendance(studentIndex);
                OLED_ShowAttendanceSummary();
                for(volatile uint32_t j=0; j<1000000; j++);
            }
        }
    }
}