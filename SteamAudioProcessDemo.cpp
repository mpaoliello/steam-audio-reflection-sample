#include "SteamAudioProcess.h"
#include <direct.h>   // For _mkdir on Windows
#include <iostream>

#pragma pack(push,1)
typedef struct {
	char riff[4]; unsigned int size; char wave[4];
	char fmt_[4]; unsigned int fmtSize; unsigned short format;
	unsigned short channels; unsigned int sampleRate;
	unsigned int byteRate; unsigned short blockAlign; unsigned short bitsPerSample;
} WavHeader;
#pragma pack(pop)

static bool load_wav_mono_f32(const char* p, float** samples, int* n, int* sr) {
	*samples = NULL; *n = 0; *sr = 0;
	FILE* f = NULL;
	if (fopen_s(&f, p, "rb") != 0 || !f) return false;
	WavHeader h; if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); return false; }
	if (memcmp(h.riff, "RIFF", 4) || memcmp(h.wave, "WAVE", 4) || h.format != 3 || h.channels != 1 || h.bitsPerSample != 32) { fclose(f); return false; }
	char tag[4]; unsigned int sz = 0;
	while (fread(tag, 1, 4, f) == 4) { if (fread(&sz, 4, 1, f) != 1) { fclose(f); return false; } if (!memcmp(tag, "data", 4)) break; fseek(f, sz, SEEK_CUR); }
	int count = sz / sizeof(float);
	float* buf = (float*)malloc(sz); if (!buf) { fclose(f);return false; }
	if (fread(buf, sizeof(float), count, f) != (size_t)count) { free(buf); fclose(f); return false; }
	fclose(f); *samples = buf; *n = count; *sr = h.sampleRate; return true;
}

static int write_wav_stereo_f32(const char* p, const float* L, const float* R, int frames, int sr) {
	FILE* f = NULL;
	if (fopen_s(&f, p, "wb") != 0 || !f) return false;
	const int bytes = frames * 2 * 4;
	WavHeader h = { 0 }; memcpy(h.riff, "RIFF", 4); memcpy(h.wave, "WAVE", 4);
	memcpy(h.fmt_, "fmt ", 4); h.fmtSize = 16; h.format = 3; h.channels = 2; h.sampleRate = sr;
	h.byteRate = sr * 2 * 4; h.blockAlign = 2 * 4; h.bitsPerSample = 32; h.size = 4 + (8 + h.fmtSize) + (8 + bytes);
	fwrite(&h, sizeof(h), 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
	float* inter = (float*)malloc(bytes); if (!inter) { fclose(f);return false; }
	for (int i = 0;i < frames;++i) { inter[2 * i] = L[i]; inter[2 * i + 1] = R[i]; }
	fwrite(inter, sizeof(float), frames * 2, f); free(inter); fclose(f); return true;
}

// Function to create directory if it doesn't exist
bool create_output_directory(const char* dirname) {
	// Windows: use _mkdir
	if (_mkdir(dirname) == 0) {
		return true;
	}
	else if (errno == EEXIST) {
		// Directory already exists, which is fine
		return true;
	}
	else {
		// Error creating directory
		return false;
	}
}

int main()
{
	const char* output_dir = ".\\media";

	if (!create_output_directory(output_dir)) {
		std::cerr << "Failed to create output directory: " << output_dir << std::endl;
		return 1;
	}
	float* mono;
	int total = 0;
	int sampleRate = 0;


	SteamAudioProcess sap = SteamAudioProcess();
	sap.numSources = 1;
	float* sourceBuffer = (float*)calloc(sap.frameSize, sizeof(float));
	sap.sourcesData.push_back(sourceBuffer);
	sap.sourcesPositions.push_back(std::vector<float>{ 0.0f, 1.7f, -3.0f });

	std::string filename = "jazz";
	//std::string filename = "LinkinPark_InTheEnd";
	
	if (!load_wav_mono_f32((std::string(output_dir) + "\\" + filename + ".wav").c_str(), &mono, &total, &sampleRate)) { fprintf(stderr, "Expected mono 32-bit float WAV.\n"); return 1; }

	const int framesTotal = (total + sap.frameSize - 1) / sap.frameSize;
	const int bufferSize = framesTotal * sap.frameSize;
	std::cout << "Processing " << total << " samples in " << framesTotal << " frames of " << sap.frameSize << " samples each.\n";
	float* DL = (float*)calloc(bufferSize, sizeof(float)),
		* DR = (float*)calloc(bufferSize, sizeof(float)),
		* EL = (float*)calloc(bufferSize, sizeof(float)),
		* ER = (float*)calloc(bufferSize, sizeof(float)),
		* RL = (float*)calloc(bufferSize, sizeof(float)),
		* RR = (float*)calloc(bufferSize, sizeof(float));

	sap.awake();
	sap.onEnable();

	int cursor = 0, fidx = 0;
	while (cursor < total)
	{
		size_t ofs = fidx * sap.frameSize;
		for (int i = 0;i < sap.frameSize;++i)
			sap.sourcesData[0][i] = (cursor + i < total) ? mono[cursor + i] : 0.0f;

		sap.update();

		//std::this_thread::sleep_for(std::chrono::milliseconds(1));

		// accumulate
		for (int i = 0;i < sap.frameSize;++i) {
			int idx = fidx * sap.frameSize + i;
			//DL[idx] += sap.directAudioBuffer.data[0][i]; DR[idx] += sap.directAudioBuffer.data[1][i];
			//EL[idx] += sap.reflectionsAudioBuffer.data[0][i];  ER[idx] += sap.reflectionsAudioBuffer.data[1][i];
			//RL[idx] += sap.reverbAudioBuffer.data[0][i]; RR[idx] += sap.reverbAudioBuffer.data[0][i];

			DL[idx] += sap.directAudio[i]; DR[idx] += sap.directAudio[i + sap.frameSize];
			EL[idx] += sap.reflectionsAudio[i];  ER[idx] += sap.reflectionsAudio[i + sap.frameSize];
			RL[idx] += sap.reverbAudio[i]; RR[idx] += sap.reverbAudio[i];
		}

		cursor += sap.frameSize;
		fidx++;
	}

	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_direct.wav").c_str(), DL, DR, bufferSize, sampleRate);
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_early.wav").c_str(), EL, ER, bufferSize, sampleRate);
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_reverb.wav").c_str(), RL, RR, bufferSize, sampleRate);

	sap.onDisable();
	sap.onDestroy();

	free(mono); free(DL); free(DR); free(EL); free(ER); free(RL); free(RR);

	return 0;
}