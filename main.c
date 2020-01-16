#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

typedef struct {
	uint32_t ChunkID;
	uint32_t ChunkSize;
	uint32_t Format;
	/// fmt
	uint32_t Subchunk1ID;
	uint32_t Subchunk1Size;
	uint16_t AudioFormat;
	uint16_t NumChannels;
	uint32_t SampleRate;
	uint32_t ByteRate;
	uint16_t BlockAlign;
	uint16_t BitsPerSample;
	/// data
	uint32_t Subchunk2ID;
	uint32_t Subchunk2Size;
}__attribute__((packed)) sWavHeader;

uint32_t numOfSamples = 0;
int32_t lowLimit;
int32_t highLimit;
int16_t * leftChannel = NULL;
int16_t * rightChannel = NULL;

uint16_t DAC_max = 255;
uint16_t DAC_min = 0;
int32_t rawmin = 0;
int32_t rawmax = 1;//prevent division by zero

/// DAC resolution set for C format
uint16_t ConvertToDACValue(int16_t value) {
	float tmp = (float) value;
	tmp = tmp - rawmin;
	tmp = tmp / (rawmax - rawmin);
	tmp = tmp * (DAC_max - DAC_min);
	tmp = tmp + DAC_min;
	uint32_t tmp2 = round(tmp);
	return tmp2;
}

void swapEndiannes(uint32_t * value) {
	uint32_t tmp = *value;
	*value = (tmp >> 24) | (tmp << 24) | ((tmp >> 8) & 0x0000FF00)
			| ((tmp << 8) & 0x00FF0000);
}

void swapEndiannesS16(int16_t * value) {
	uint32_t tmp = *value;
	*value = (tmp >> 8) | (tmp << 8);
}

void printfU32String(uint32_t array) {
	char text[5];
	text[0] = (array >> 24) & 0xFF;
	text[1] = (array >> 16) & 0xFF;
	text[2] = (array >> 8) & 0xFF;
	text[3] = array & 0xFF;
	text[4] = 0;
	printf(" %s.\n", text);
}

void printfHeader(sWavHeader * header) {
	printf("WAV HEADER.\n");
	printf("--------------------------------------.\n");
	printf("ChunkID : ");
	swapEndiannes(&header->ChunkID);
	printfU32String(header->ChunkID);
	printf("ChunkSize : %d.\n", header->ChunkSize);
	printf("Format : ");
	swapEndiannes(&header->Format);
	printfU32String(header->Format);
	/// fmt
	printf("Subchunk1ID : ");
	swapEndiannes(&header->Subchunk1ID);
	printfU32String(header->Subchunk1ID);
	printf("Subchunk1Size : %d.\n", header->Subchunk1Size);
	if (header->AudioFormat == 1) {
		printf("AudioFormat : PCM.\n");
	} else {
		printf("AudioFormat : Compression.\n");
	}
	printf("NumChannels : %d.\n", header->NumChannels);
	printf("SampleRate : %d.\n", header->SampleRate);
	printf("ByteRate : %d.\n", header->ByteRate);
	printf("BlockAlign : %d.\n", header->BlockAlign);
	printf("BitsPerSample : %d.\n", header->BitsPerSample);
	/// data
	printf("Subchunk2ID : ");
	swapEndiannes(&header->Subchunk2ID);
	printfU32String(header->Subchunk2ID);
	printf("Subchunk2Size : %d.\n", header->Subchunk2Size);
	printf("--------------------------------------.\n");
}

void fprintfChannel(FILE *pFile, int16_t * array, uint32_t size) {
	for (uint32_t i = 0; i < size; ++i) {
		fprintf(pFile, "0x%02x, // %d\n", ConvertToDACValue(array[i]),
				array[i]);
	}
}

void readWavData(int fileId, sWavHeader * header) {
	numOfSamples = header->Subchunk2Size
			/ (header->NumChannels * header->BitsPerSample / 8);
	switch (header->BitsPerSample) {
	case 8:
		lowLimit = -128;
		highLimit = 127;
		break;
	case 16:
		lowLimit = -32768;
		highLimit = 32767;
		break;
	case 32:
		lowLimit = -2147483648;
		highLimit = 2147483647;
		break;
	}

	leftChannel = malloc(sizeof(uint16_t) * numOfSamples);
	if (header->NumChannels > 1) {
		rightChannel = malloc(sizeof(uint16_t) * numOfSamples);
	}

	/// read all music data
	for (uint32_t i = 0; i < numOfSamples; ++i) {
		read(fileId, &leftChannel[i], sizeof(uint16_t));
		if (leftChannel[i] < rawmin)
			rawmin = leftChannel[i];
		if (leftChannel[i] > rawmax)
			rawmax = leftChannel[i];
//    swapEndiannesS16(&leftChannel[i]);
		// check if value was in range
		if ((leftChannel[i] < lowLimit) || (leftChannel[i] > highLimit)) {
			printf("**value out of range\n");
		}

		if (header->NumChannels > 1) {
			read(fileId, &rightChannel[i], sizeof(uint16_t));
			if (rightChannel[i] < rawmin)
				rawmin = rightChannel[i];
			if (rightChannel[i] > rawmax)
				rawmax = rightChannel[i];
//      swapEndiannesS16(&rightChannel[i]);
			// check if value was in range
			if ((rightChannel[i] < lowLimit) || (rightChannel[i] > highLimit)) {
				printf("**value out of range\n");
			}
		}
	}
}

void writeAsCFile(int16_t *leftChannel, int16_t *rightChannel,
		sWavHeader * header, const char *filename) {
	FILE * pFile;
	pFile = fopen(filename, "w");

	fprintf(pFile, "const uint8_t data[] = {\n");
	fprintfChannel(pFile, leftChannel, numOfSamples);
	fprintf(pFile, "};\n");

	if (header->NumChannels > 1) {
		/// printf right Channel
		fprintf(pFile, "const uint8_t data2[] = {\n");
		fprintfChannel(pFile, rightChannel, numOfSamples);
		fprintf(pFile, "};\n");
	}
	fclose(pFile);
}

int main(int argc, char * argv[]) {
	char fname[256];
	sWavHeader wavHeader;
	/// check arguments
	if (argc < 2) {
		printf("No arguments!.\n");
		return -1;
	}

	/// read header
	printf("Opening file %s.\n", argv[1]);
	int file = open(argv[1], O_RDONLY);
	read(file, &wavHeader, sizeof(sWavHeader));
	printfHeader(&wavHeader);
	readWavData(file, &wavHeader);
	close(file);

	sprintf(fname, "%s.c", argv[1]);
	writeAsCFile(leftChannel, rightChannel, &wavHeader, fname);

	return 0;
}
