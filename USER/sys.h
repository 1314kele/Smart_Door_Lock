#ifndef _SYS_H_
#define _SYS_H_

#define PAin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOA_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PAout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOA_BASE-0x40000000+0x14)<<5) + (x<<2))	

#define PBin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOB_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PBout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOB_BASE-0x40000000+0x14)<<5) + (x<<2))

#define PCin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOC_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PCout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOC_BASE-0x40000000+0x14)<<5) + (x<<2))	

#define PDin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOD_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PDout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOD_BASE-0x40000000+0x14)<<5) + (x<<2))

#define PEin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOE_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PEout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOE_BASE-0x40000000+0x14)<<5) + (x<<2))	

#define PFin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOF_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PFout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOF_BASE-0x40000000+0x14)<<5) + (x<<2))
	
#define PGin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOG_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PGout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOG_BASE-0x40000000+0x14)<<5) + (x<<2))	

#define PHin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOH_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PHout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOH_BASE-0x40000000+0x14)<<5) + (x<<2))
	
#define PIin(x) *(volatile unsigned int *)(0x42000000 + ((GPIOI_BASE-0x40000000+0x10)<<5) + (x<<2))
#define PIout(x) *(volatile unsigned int *)(0x42000000 + ((GPIOI_BASE-0x40000000+0x14)<<5) + (x<<2))	

#endif
