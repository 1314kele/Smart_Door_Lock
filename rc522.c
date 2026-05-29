#include "stm32f4xx.h"
#include "delay.h" 
#include "rc522.h"
					  
//SPI初始化
void MF522SPI_Init(void)
{	 
	GPIO_InitTypeDef  GPIO_InitStructure;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC|RCC_AHB1Periph_GPIOD, ENABLE);//GPIOC GPIOD

	//GPIO PC6 MOSI
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;					//PC6
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;				//输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;				//推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//100MHz
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;				//上拉
	GPIO_Init(GPIOC, &GPIO_InitStructure);						//初始化
	
	//GPIO PC8 MISO
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;					//PC8
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;				//输入模式
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//100MHz
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;				//上拉
	GPIO_Init(GPIOC, &GPIO_InitStructure);						//初始化
	
	//GPIO PD6 PD7 片选 复位
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6|GPIO_Pin_7;		//PD6 PD7
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;				//输出模式
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//100MHz
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;			//上拉
	GPIO_Init(GPIOD, &GPIO_InitStructure);						//初始化
 
}   

//SPI读写一个字节
//TxData:要写入的数据
//返回值:读取到的数据
uint8_t MF522SPI_ReadWriteByte(uint8_t TxData)
{		 			 

	uint8_t i=0,d=0;

	MF522_SCK=0;
	delay_us(5);
	for(i=0; i<8; i++)
	{
		if(TxData & (1<<(7-i)))
		{
			 MF522_MOSI=1;
		}
		else
		{
			 MF522_MOSI=0;
		}

		MF522_SCK=1;
		delay_us(5);
	
		if(MF522_MISO)
		{
			 d|=1<<(7-i);
		}
			
		MF522_SCK=0;
		delay_us(5);
	}	
	
	
	return d;	
}


void MF522_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	
	MF522SPI_Init();

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);//GPIOC GPIOD

	//GPIO PC11 复位引脚
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;					//PC11
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;				//输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;				//推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;			//100MHz
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;				//上拉
	GPIO_Init(GPIOC, &GPIO_InitStructure);						//初始化
	
	MF522_RST=1; //复位引脚拉高

	MFRC522_Reset();         

}

void MF522SPI_Send(uint8_t val)  
{ 
	MF522SPI_ReadWriteByte(val);
}
//
uint8_t MF522SPI_Recv(void)  
{ 
	uint8_t temp; 

	temp=MF522SPI_ReadWriteByte(0xFF);

	return temp; 
}

//用SPI写一个字节到MFRC522的一个寄存器中
//reg:指定的寄存器地址 val:要写入的值
void Write_MFRC522(uint8_t addr, uint8_t val) 
{
	//格式:0XXXXXX0  
	MF522_NSS=0;  
	delay_us(5);
	MF522SPI_Send((addr<<1)&0x7E);  
	MF522SPI_Send(val);  
	delay_us(5);
	MF522_NSS=1; 
}
//用SPI从MFRC522的一个寄存器中读一个字节
//reg:指定的寄存器地址
//返回值:读取得寄存器的值  
uint8_t Read_MFRC522(uint8_t addr) 
{  
	uint8_t val;
	//格式:1XXXXXX0    
	MF522_NSS=0; 
	delay_us(5);
	MF522SPI_Send(((addr<<1)&0x7E)|0x80);   
	val=MF522SPI_Recv();   
	delay_us(5);
	MF522_NSS=1; 
	//   
	return val;  
}
//置位MFRC522的某一位
//reg:指定的寄存器;mask:置位值
void SetBitMask(uint8_t reg, uint8_t mask)   
{     
	uint8_t tmp=0;
	//     
	tmp=Read_MFRC522(reg);     
	Write_MFRC522(reg,tmp|mask);  // set bit mask 
}
//清位MFRC522的某一位
//reg:指定的寄存器;mask:清位值
void ClearBitMask(uint8_t reg, uint8_t mask)   
{     
	uint8_t tmp=0;
	//     
	tmp=Read_MFRC522(reg);     
	Write_MFRC522(reg,tmp&(~mask));  //clear bit mask 
}
//打开天线,每次发送或接收命令后都要重新打开天线
void AntennaOn(void) 
{  
	uint8_t temp;
	//   
	temp=Read_MFRC522(TxControlReg);  
	if ((temp&0x03)==0)  
	{   
		SetBitMask(TxControlReg,0x03);  
	}
}
//关闭天线,每次发送或接收命令后都要重新关闭天线
void AntennaOff(void) 
{  
	ClearBitMask(TxControlReg,0x03);
}

//复位MFRC522
void MFRC522_Reset(void) 
{ 
	//复位引脚
	MF522_RST=1;
	delay_us(1);
	MF522_RST=0;
	delay_us(1);
	MF522_RST=1;
	delay_us(1); 
	//发送复位命令     
	Write_MFRC522(CommandReg, PCD_RESETPHASE); 
	
	//Timer: TPrescaler*TreloadVal/6.78MHz = 0xD3E*0x32/6.78=25ms     
	Write_MFRC522(TModeReg,0x8D);				//TAuto=1,自动启动定时器
	//Write_MFRC522(TModeReg,0x1D);				//TAutoRestart=1，0.5ms中断
	Write_MFRC522(TPrescalerReg,0x3E); 	//定时器预分频     
	Write_MFRC522(TReloadRegL,0x32);		//定时器重装值低8位                
	Write_MFRC522(TReloadRegH,0x00);		//定时器重装值高8位       
	Write_MFRC522(TxAutoReg,0x40); 			//100%ASK     
	Write_MFRC522(ModeReg,0x3D); 				//CRC初始值0x6363
	Write_MFRC522(CommandReg,0x00);			//空闲状态MFRC522  
	//Write_MFRC522(RFCfgReg, 0x7F);    //RxGain = 48dB      
	AntennaOn();          							//打开天线 	
}
//

//通过RC522和ISO14443卡通讯
//command:MF522命令字
//					sendData:通过RC522发送到卡片的数据
//					sendLen:发送数据的长度
//					BackData:接收到的数据
//					BackLen:接收数据的位长度
//返回值:成功返回MI_OK
u8 MFRC522_ToCard(u8 command, u8 *sendData, u8 sendLen, u8 *backData, u16 *backLen) 
{
	u8  status=MI_ERR;
	u8  irqEn=0x00;
	u8  waitIRq=0x00;
	u8  lastBits;
	u8  n;
	u16 i;
	//根据命令设置中断
	switch (command)     
	{         
		case PCD_AUTHENT:  		//验证密码   
			irqEn 	= 0x12;			//    
			waitIRq = 0x10;			//    
			break;
		case PCD_TRANSCEIVE: 	//发送FIFO数据      
			irqEn 	= 0x77;			//    
			waitIRq = 0x30;			//    
			break;      
		default:    
			break;     
	}
	//
	Write_MFRC522(ComIEnReg, irqEn|0x80);		//允许中断请求     
	ClearBitMask(ComIrqReg, 0x80);  			//清除所有中断请求标志位               	
	SetBitMask(FIFOLevelReg, 0x80);  			//FlushBuffer=1, FIFO初始化
	Write_MFRC522(CommandReg, PCD_IDLE); 		//停止MFRC522命令   
	//写数据到FIFO     
	for (i=0; i<sendLen; i++)
		Write_MFRC522(FIFODataReg, sendData[i]);
	//执行命令
	Write_MFRC522(CommandReg, command);
	//如果是发送数据命令     
	if (command == PCD_TRANSCEIVE)					//执行发送命令      
		SetBitMask(BitFramingReg, 0x80);  		//StartSend=1,transmission of data starts      
	//等待接收数据完成     
	i = 10000; //i调整时间M1卡通信超时25ms     
	do      
	{        
		n = Read_MFRC522(ComIrqReg);
		//irq_regdata=n;	//test         
		i--;
		//wait_count=i;		//test		     
	}while ((i!=0) && !(n&0x01) && !(n&waitIRq));	//等待中断n=0x64
	//清除启动发送位
	ClearBitMask(BitFramingReg, 0x80);   		//StartSend=0
	//判断是否在25ms内完成
	if (i != 0)     
	{            
		if(!(Read_MFRC522(ErrorReg) & 0x1B)) //BufferOvfl Collerr CRCErr ProtecolErr         
		{            
			if (n & irqEn & 0x01)			//                  
				status = MI_NOTAGERR;		//
			//
			if (command == PCD_TRANSCEIVE)             
			{                 
				n = Read_MFRC522(FIFOLevelReg);		//n=0x02                
				lastBits = Read_MFRC522(ControlReg) & 0x07;	//lastBits=0               
				if (lastBits!=0)                         
					*backLen = (n-1)*8 + lastBits; 
				else
					*backLen = n*8;									//backLen=0x10=16
				//
				if (n == 0)                         
				 	n = 1;                        
				if (n > MAX_LEN)         
				 	n = MAX_LEN;
				//
				for (i=0; i<n; i++)                 
					backData[i] = Read_MFRC522(FIFODataReg); 
			}
			//
			status = MI_OK;		
		}
		else
			status = MI_ERR;
	}	
	//
	//Write_MFRC522(ControlReg,0x80);				//timer stops     
	//Write_MFRC522(CommandReg, PCD_IDLE);	//
	//
	return status;
}
//寻卡，读取卡类型号
//reqMode:寻卡方式
//					TagType:返回卡类型
//					0x4400 = Mifare_UltraLight
//					0x0400 = Mifare_One(S50)
//					0x0200 = Mifare_One(S70)
//					0x0800 = Mifare_Pro(X)
//					0x4403 = Mifare_DESFire
//返回值:成功返回MI_OK	
u8 MFRC522_Request(u8 reqMode, u8 *TagType)
{  
	u8  status;    
	u16 backBits;   //接收到的数据位长度
	//   
	Write_MFRC522(BitFramingReg, 0x07);  //TxLastBists = BitFramingReg[2..0]   
	TagType[0] = reqMode;  
	status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits); 
	// 
	if ((status != MI_OK) || (backBits != 0x10))  
	{       
		status = MI_ERR;
	}
	//  
	return status; 
}
//防冲突，读取卡序列号
//serNum:返回4字节卡序列号，第5字节为校验字节
//返回值:成功返回MI_OK
u8 MFRC522_Anticoll(u8 *serNum) 
{     
	u8  status;     
	u8  i;     
	u8  serNumCheck=0;     
	u16 unLen;
	//           
	ClearBitMask(Status2Reg, 0x08);  			//TempSensclear     
	ClearBitMask(CollReg,0x80);   				//ValuesAfterColl  
	Write_MFRC522(BitFramingReg, 0x00);  	//TxLastBists = BitFramingReg[2..0]
	serNum[0] = PICC_ANTICOLL1;     
	serNum[1] = 0x20;     
	status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);
	//      
	if (status == MI_OK)
	{   
		//校验校验和   
		for(i=0;i<4;i++)   
			serNumCheck^=serNum[i];
		//
		if(serNumCheck!=serNum[i])        
			status=MI_ERR;
	}
	SetBitMask(CollReg,0x80);  //ValuesAfterColl=1
	//      
	return status;
}
//用MF522计算CRC
//pIndata:要计算CRC的数据 len:数据长度 pOutData:计算结果
void CalulateCRC(u8 *pIndata, u8 len, u8 *pOutData) 
{     
	u16 i;
	u8  n;
	//      
	ClearBitMask(DivIrqReg, 0x04);   			//CRCIrq = 0     
	SetBitMask(FIFOLevelReg, 0x80);   		//清空FIFO     
	Write_MFRC522(CommandReg, PCD_IDLE);   
	//写数据到FIFO      
	for (i=0; i<len; i++)
		Write_MFRC522(FIFODataReg, *(pIndata+i));
	//执行CRC命令
	Write_MFRC522(CommandReg, PCD_CALCCRC);
	//等待CRC计算完成     
	i = 1000;     
	do      
	{         
		n = Read_MFRC522(DivIrqReg);         
		i--;     
	}while ((i!=0) && !(n&0x04));   //CRCIrq = 1
	//读取CRC计算结果     
	pOutData[0] = Read_MFRC522(CRCResultRegL);     
	pOutData[1] = Read_MFRC522(CRCResultRegH);
	Write_MFRC522(CommandReg, PCD_IDLE);
}
//选卡，读取卡存储器容量
//serNum:传入卡序列号
//返回值:成功返回卡容量
u8 MFRC522_SelectTag(u8 *serNum) 
{     
	u8  i;     
	u8  status;     
	u8  size;     
	u16 recvBits;     
	u8  buffer[9];
	//     
	buffer[0] = PICC_ANTICOLL1;	//防碰撞命令1     
	buffer[1] = 0x70;
	buffer[6] = 0x00;						     
	for (i=0; i<4; i++)					
	{
		buffer[i+2] = *(serNum+i);	//buffer[2]-buffer[5]是卡序列号
		buffer[6]  ^=	*(serNum+i);	//校验字节
	}
	//
	CalulateCRC(buffer, 7, &buffer[7]);	//buffer[7]-buffer[8]是RCR校验
	ClearBitMask(Status2Reg,0x08);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);
	//
	if ((status == MI_OK) && (recvBits == 0x18))    
		size = buffer[0];     
	else    
		size = 0;
	//	     
	return size; 
}
//验证卡密码
//authMode:密码验证方式
//					0x60 = 验证A密码
//					0x61 = 验证B密码
//					BlockAddr:块地址
//					Sectorkey:扇区密码
//					serNum:卡序列号4字节
//返回值:成功返回MI_OK
u8 MFRC522_Auth(u8 authMode, u8 BlockAddr, u8 *Sectorkey, u8 *serNum) 
{     
	u8  status;     
	u16 recvBits;     
	u8  i;  
	u8  buff[12];    
	//命令+块号+扇区密码+卡序列号     
	buff[0] = authMode;		//命令     
	buff[1] = BlockAddr;	//块号     
	for (i=0; i<6; i++)
		buff[i+2] = *(Sectorkey+i);	//扇区密码
	//
	for (i=0; i<4; i++)
		buff[i+8] = *(serNum+i);		//卡序列号
	//
	status = MFRC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);
	//      
	if ((status != MI_OK) || (!(Read_MFRC522(Status2Reg) & 0x08)))
		status = MI_ERR;
	//
	return status;
}
//读块数据
//blockAddr:块地址;recvData:读取到的数据
//返回值:成功返回MI_OK
u8 MFRC522_Read(u8 blockAddr, u8 *recvData) 
{     
	u8  status;     
	u16 unLen;
	//      
	recvData[0] = PICC_READ;     
	recvData[1] = blockAddr;     
	CalulateCRC(recvData,2, &recvData[2]);     
	status = MFRC522_ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen);
	//
	if ((status != MI_OK) || (unLen != 0x90))
		status = MI_ERR;
	//
	return status;
}
//写块数据
//blockAddr:块地址;writeData:要写入的16字节数据
//返回值:成功返回MI_OK
u8 MFRC522_Write(u8 blockAddr, u8 *writeData) 
{     
	u8  status;     
	u16 recvBits;     
	u8  i;  
	u8  buff[18];
	//           
	buff[0] = PICC_WRITE;     
	buff[1] = blockAddr;     
	CalulateCRC(buff, 2, &buff[2]);     
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	//
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))
		status = MI_ERR;
	//
	if (status == MI_OK)     
	{         
		for (i=0; i<16; i++)  //写FIFO16Byte数据                     
			buff[i] = *(writeData+i);
		//                     
		CalulateCRC(buff, 16, &buff[16]);         
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);           
		if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A))               
			status = MI_ERR;         
	}          
	return status;
}
//命令卡片进入休眠状态
void MFRC522_Halt(void) 
{    
	u16 unLen;     
	u8  buff[4];
	//       
	buff[0] = PICC_HALT;     
	buff[1] = 0;     
	CalulateCRC(buff, 2, &buff[2]);       
	MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff,&unLen);
}
