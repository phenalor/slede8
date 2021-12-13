#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>

uint8_t *memory;
uint32_t max_ticks = 1 << 31;
uint32_t tick = 0;
uint32_t pc = 0, backtrace[1 << 10];
uint16_t backtrace_ptr = 0;
uint8_t flag = 0;
uint8_t regs[16];

const char *magic = ".SLEDE8";

uint8_t ret = 0;

FILE *fp;
uint32_t program_size;


uint16_t *instruction;
uint8_t operation_class;
uint8_t operation;
uint16_t address;
uint8_t value;
uint8_t argument1;
uint8_t argument2;

uint16_t addr;

int bytes_read, bytes_written;
uint8_t the_byte;
uint32_t input_count = 0, output_count = 0;

struct timeval tv;
fd_set fds;

uint16_t timeout = 1;

int main(int argc, char **argv)
{
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	if(argc != 2) 
	{
		fprintf(stderr, "Exactly one argument required: a .SLEDE8 file");
		return 1;
	}
	memset(backtrace, 0, sizeof(backtrace));
	fp = fopen(argv[1], "r");
	if(!fp)
	{
		fprintf(stderr, "Couldnt open file: %s\n", argv[1]);
		return 2;
	}
	fseek(fp, 0L, SEEK_END);
	program_size = ftell(fp);
	if(program_size <= strlen(magic))
	{
		fprintf(stderr, "Program size too small: %u\n", program_size);
		return 3;
	}
	memory = malloc(program_size - strlen(magic));
	program_size -= strlen(magic);
	fseek(fp, 0L, SEEK_SET);
	bytes_read = fread(memory, 1, strlen(magic), fp);
	if(strncmp(magic, (char*)memory, strlen(magic)))
	{
		fprintf(stderr, "Incorrect header: \"%*s\" != \"%*s\"\n", (int)strlen(magic), memory, (int)strlen(magic), magic);
		ret = 4;
		goto EXIT;
	}
	bytes_read = fread(memory, 1, program_size, fp);
	fclose(fp);
	if(bytes_read > 0 && (uint32_t)bytes_read != program_size)
	{
		fprintf(stderr, "Unexpected data size: %u != %u\n", bytes_read, program_size);
		ret = 5;
		goto EXIT;
	}


#ifdef DEBUG
	fp = fopen("slede8.debug", "w");
#endif

	while(pc + 1u < program_size)
	{
		tick += 1u;
		if(tick > max_ticks)
		{
			fprintf(stderr, "Max ticks exceeded\n");
			ret = 6;
			goto EXIT;
		}
		instruction = (uint16_t*)(memory + pc);
		operation_class = *instruction & 0xf;
		operation = (*instruction >> 4) & 0xf;
		address = *instruction >> 4;
		value = *instruction >> 8;
		argument1 = (*instruction >> 8) & 0xf;
		argument2 = (*instruction >> 12) & 0xf;
		pc += 2;

#ifdef DEBUG
		fprintf(fp, "%02x %02x %04x %02x %02x %02x -", operation_class, operation, address, value, argument1, argument2); 
		for(uint8_t i = 0; i < sizeof(regs); ++i) fprintf(fp, " %02x", regs[i]);
		fprintf(fp, "\n");
#endif

		switch(operation_class)
		{
			case 0x0:
				goto EXIT;
			case 0x01:
				regs[operation] = value;
				break;
			case 0x02:
				regs[operation] = regs[argument1];
				break;
			case 0x3:
				regs[1] = (address & 0x0f00) >> 8;
				regs[0] = address & 0xff;
				break;
			case 0x04:
				addr = ((regs[1] << 8) | regs[0]) & 0x0fff;
				if(addr >= program_size)
				{
					fprintf(stderr, "Address out of bounds, %u > %u\n", addr, program_size);
					ret = 7;
					goto EXIT;
				}
				switch(operation)
				{
					case 0x00:
						regs[argument1] = memory[addr];
						break;
					case 0x01:
						memory[addr] = regs[argument1];
						break;
				}
				break;
			case 0x05:
				switch(operation)
				{
					case 0x00:
						regs[argument1] &= regs[argument2];
						break;
					case 0x01:
						regs[argument1] |= regs[argument2];
						break;
					case 0x02:
						regs[argument1] ^= regs[argument2];
						break;
					case 0x03:
						regs[argument1] = regs[argument1] << regs[argument2];
						break;
					case 0x04:
						regs[argument1] >>= regs[argument2];
						break;
					case 0x05:
						regs[argument1] = regs[argument1] + regs[argument2];
						break;
					case 0x06:
						regs[argument1] = regs[argument1] - regs[argument2];
						break;
				}
				break;
			case 0x06:
				switch(operation)
				{
					case 0x00:
						tv.tv_sec = timeout;
						tv.tv_usec = 0;
						select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
						if(!FD_ISSET(STDIN_FILENO, &fds))
						{
							fprintf(stderr, "Nothing to read from stdin\n");
							ret = 8;
							goto EXIT;
						}
						bytes_read = fread(&the_byte, 1, 1, stdin);
						if(bytes_read != 1)
						{
							fprintf(stderr, "Failed to read byte from stdin\n");
							ret = 8;
							goto EXIT;
						}
						input_count += 1;
						regs[argument1] = the_byte;
						break;
					case 0x01:
						bytes_written = fwrite(&regs[argument1], 1, 1, stdout);
						if(bytes_written != 1)
						{
							fprintf(stderr, "Failed to write byte to stdout\n");
							ret = 9;
							goto EXIT;
						}
						output_count += 1;
						break;
				}
				break;
			case 0x07:
				switch(operation)
				{
					case 0x00:
						flag = regs[argument1] == regs[argument2];
						break;
					case 0x01:
						flag = regs[argument1] != regs[argument2];
						break;
					case 0x02:
						flag = regs[argument1] < regs[argument2];
						break;
					case 0x03:
						flag = regs[argument1] <= regs[argument2];
						break;
					case 0x04:
						flag = regs[argument1] > regs[argument2];
						break;
					case 0x05:
						flag = regs[argument1] >= regs[argument2];
						break;
				}
				break;
			case 0x08:
				pc = address;
				break;
			case 0x09:
				if(flag) pc = address;
				break;
			case 0x0a:
				if(backtrace_ptr + 1u >= sizeof(backtrace))
				{
					fprintf(stderr, "Backtrace limit exceeded: %lu\n", sizeof(backtrace));
					ret = 10;
					goto EXIT;
				}
				backtrace[backtrace_ptr++] = pc;
				pc = address;
				break;
			case 0x0b:
				if(backtrace_ptr == 0)
				{
					fprintf(stderr, "Backtrace stack empty\n");
					ret = 11;
					goto EXIT;
				}
				pc = backtrace[--backtrace_ptr];
				break;
			case 0x0c:
				break;
			default:
				fprintf(stderr, "Unknown instruction: %02x\n", operation_class);
				ret = 12;
				goto EXIT;
		}
	}

EXIT:
	if(ret) fprintf(stderr, "!!! early exit !!!\n");
#ifdef DEBUG
	fclose(fp);
#endif
	fprintf(stderr, "Read %u bytes, output %u bytes\n", input_count, output_count);
	free(memory);
	return ret;
}
