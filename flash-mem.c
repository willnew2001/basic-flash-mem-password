#include <u.h>
#include <libc.h>
#include <stdio.h>

void udelay(ulong t);
int initGpio(int pin);
int reset(int gpio_fd);
void write1W(int gpio_fd, uchar com);
uchar read1W(int gpio_fd);
int writeScratchCom(int gpio_fd, ushort addr, char* data, int size);
int readScratchCom(int gpio_fd, char* auth);
int copyScratchCom(int gpio_fd, char* auth);
int writeToMem(int gpio_fd, ushort addr, char* data, int size);
int readMem(int gpio_fd, ushort addr, char* data, int sizeToRead);

/* Custom delay function in microseconds */
void
udelay(ulong t)
{
	ulong i;
	t*=747;  //calibrated so one execution of udelay takes 1 microsecond
	for(i=0;i<t;i++);
}

/* Initialize the GPIO pin for the 1 wire device */
int
initGpio(int pin)
{
	int gpio_fd;

	gpio_fd = open("/dev/gpio", ORDWR);

	/* Check for error and try once more */
	if (gpio_fd < 0) {
		bind("#G", "/dev", MAFTER);
		gpio_fd = open("/dev/gpio", ORDWR);
		if (gpio_fd < 0) return gpio_fd;
	}

	/* Set as input and pull low */
	fprint(gpio_fd, "function %d in", pin);
	fprint(gpio_fd, "set %d 0", pin);

	return gpio_fd;
}

/* Send a reset pulse to the 1 wire device */
int
reset(int gpio_fd)
{
	char buf[17];
	uvlong x;

	/* Reset pulse */
	fprint(gpio_fd, "function 27 out");
	udelay(700);
	fprint(gpio_fd, "function 27 in");
	udelay(80);

	/* Read GPIO */
	if (read(gpio_fd, buf,16) < 0) {
		print("error reading while resetting\n");
	}

	/* Verify communication */
	buf[16] = 0;
	
	x = strtoull(buf, nil, 16);
	udelay(100);

	return x & (1 << 27);
}

/* Write a byte to the 1 wire device */
void
write1W(int gpio_fd, uchar com)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (com & 0x01) {
			fprint(gpio_fd, "function 27 out");
			fprint(gpio_fd, "function 27 in");
			udelay(100);
		} else {
			fprint(gpio_fd, "function 27 out");
			udelay(60);
			fprint(gpio_fd, "function 27 in");
			udelay(40);
		}
		com >>= 1;
	}
}

/* Read a byte from the 1 wire device */
uchar
read1W(int gpio_fd)
{
	uvlong x;
	int i;
	uchar c;
	char buf[17];

	c = 0;
	for (i = 0; i < 8; i++) {
		c >>= 1;
		write(gpio_fd, "function 27 pulse", 17);

		/* Read GPIO */
		if (read(gpio_fd, buf, 16) < 0) {
			print("read error\n");
		}

		buf[16] = 0;
		x = strtoull(buf, nil, 16);
		if (x & (1 << 27)) {
			c |= 0x80;
		}
		udelay(30);
	}
	return c;
}

/* Write 1 byte of data to the scratchpad */
int
writeScratchCom(int gpio_fd, ushort addr, char* data, int size)
{
	int i;

	/* Writes cannot be made of more or less than 8 bytes */
	if (size != 8) {
		return 1;
	}

	/* Reset */
	sleep(2);
	if (reset(gpio_fd)) {
		return 1;
	}
	
	/* Send write scratchpad command */
	write1W(gpio_fd, 0xCC);
	write1W(gpio_fd, 0x0F);
	
	/* Send memory address */
	write1W(gpio_fd, addr & 0x00ff); //least significant byte
	write1W(gpio_fd, addr & 0xff00); //most significant byte

	/* Send data */
	for (i = 0; i < size; i++) {
		write1W(gpio_fd, data[i]); /*transmit string and make loop*/
	}

	return 0;
}

/* Read the auth data from the scratchpad by sending the read scratchpad command */
int
readScratchCom(int gpio_fd, char* auth)
{
	int i;

	/* Reset */
	sleep(2);
	if (reset(gpio_fd)) {
		return 1;
	}

	/* Send read scratchpad command */
	write1W(gpio_fd, 0xCC);
	write1W(gpio_fd, 0xAA);
	
	/* Read authorization pattern */
	for ( i = 0; i < 3; i++) {
		auth[i] = read1W(gpio_fd);
	}

	/* If prior write was unsuccessful */
	if (auth[2] != 0x07) {
		print("Previous write to scratchpad invalid.\n");
		return 1;
	}

	return 0;
}

/* Send the copy scratchpad command to the 1 wire device */
int
copyScratchCom(int gpio_fd, char* auth)
{
	int i;

	/* Reset */
         sleep(2);
	if (reset(gpio_fd)) {
		return 1;
	}

	/* Send copy scratchpad command */
	write1W(gpio_fd, 0xCC);
	write1W(gpio_fd, 0x55);

	/* Send auth pattern */
	for (i = 0; i < 3; i++) {
		write1W(gpio_fd, auth[i]);
	}

	return 0;
}

/* Write a data buffer of any length to the user memory on the flash memory device */
int
writeToMem(int gpio_fd, ushort addr, char* data, int size)
{
	int i;
	int writeSize;
	char auth[3];
	char buf[8];

	/* If the requested write is too large for the device */
	if ((addr + size)-1 > 0x007F) {
		print("Write size too large.\n");
		return 1;
	}
	
	/* Set the write size to a multiple of 8 bytes */
	if (size < 8) {
		writeSize = 8;
	} else if (size % 8 == 0) {
		writeSize = size;
	} else {
		writeSize = size + (8 - (size % 8));
	}

	/* Write data buffer to mem in multiples of 8 bytes */
	for (i = 0; i < writeSize; i++) {
		if (i >= size) {
			buf[i%8] = 0; //filler if necesary
		} else {
			buf[i%8] = data[i];
		}

		if ( (i+1)% 8 == 0) {
			if (writeScratchCom(gpio_fd, addr, buf, 8)) return 1;
	
         		if (readScratchCom(gpio_fd, auth)) return 1;

			if (copyScratchCom(gpio_fd, auth)) return 1;

			addr += 8;
		}
	}

	return 0;
}

/* Read any amount of data off the user memory of the flash memory device */
int
readMem(int gpio_fd, ushort addr, char* data, int sizeToRead)
{
	int i;

	/* If the requested read is too large for the device */
	if ((addr + sizeToRead)-1 > 0x007F) {
		print("Read size too large.\n");
		return 1;
	}

	/* Reset */
	sleep(2);
	if (reset(gpio_fd)) {
		return 1;
	}

	/* Send read memory command */
	write1W(gpio_fd, 0xCC);
	write1W(gpio_fd, 0xF0);

	/* Send memory address */
	write1W(gpio_fd, addr & 0x00ff);
	write1W(gpio_fd, addr & 0xff00);

	/* Read data */
	sleep(2);
	for (i = 0; i < sizeToRead; i++) {
		data[i] = read1W(gpio_fd);
	}

	return 0;
}

int
main()
{
	int i, gpio_fd;
	uint char_limit = 95;
	char pass[96];
	char buf[96];
	uchar pass_flag;
	uint pass_len;

	/* Memory addresses */
	int flag_addr = 0x0000;
	int len_addr = 0x0008;
	int pass_addr = 0x0020;

	/* Set up GPIO */
	gpio_fd = initGpio(27);
	if (gpio_fd < 0) return 1;

	/* Check password flag */
	if (readMem(gpio_fd, flag_addr, buf, 1)) return 1;
	pass_flag = buf[0];

	/* If password not currently saved */
	if (pass_flag != 0xff) {

		/* Read input from user and make sure there is a null terminator */
		print("A password has not been set.\n");
		print("Please enter a password (%d character limit, extra truncated): ", char_limit);
		scanf("%s", pass);
		pass[char_limit] = '\0';

		/* Write new password to memory */
		if (writeToMem(gpio_fd, pass_addr, pass, strlen(pass)+1)) return 1;

		/* Set password flag in memory */
		buf[0] = 0xff;
		if (writeToMem(gpio_fd, flag_addr, buf, 1)) return 1;

		/* Write password len in memory */
		buf[0] = (char)strlen(pass);
		if (writeToMem(gpio_fd, len_addr, buf, 1)) return 1;


	/* If there is currently a password saved */
	} else {
		/* Read input from user and make sure there is a null terminator */
		print("Please enter your password: ");
		scanf("%s", pass);
		pass[char_limit] = '\0';

		/* Read length of password saved in memory */
		if (readMem(gpio_fd, len_addr, buf, 1)) return 1;
		pass_len = buf[0];

		/* Read password saved in memory */
		if (readMem(gpio_fd, pass_addr, buf, pass_len+1)) return 1;
		buf[pass_len] = '\0';

		/* End program if the password is incorrect */
		if (strcmp(pass, buf)) {
			print("Incorrect password.\n");
			return 0;
		}
	}

	print("Welcome!\n");

	/* Password reset functionality */
	print("Would you like to reset your password? (y/N)\n");
	scanf("%s", buf);

	if (buf[0] == 'y') {
		buf[0] = 0x00;
		/* Erase password flag */
		if (writeToMem(gpio_fd, flag_addr, buf, 1)) return 1;
		print("Password reset.\n");
	}

	return 0;
}
