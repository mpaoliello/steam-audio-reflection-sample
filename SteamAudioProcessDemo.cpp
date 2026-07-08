#pragma once

#include "SteamAudioProcess.h"
#include <direct.h>   // For _mkdir on Windows
#include <iostream>
#include "Placements.h"
#include <chrono>

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


static bool load_wav_multichannel_f32(
    const char* p,
    float*** samples,
    int* nFrames,
    int* nChannels,
    int* sr
) {
    // Initialize output values immediately.
    // This makes sure the caller never receives uninitialized data on failure.
    *samples = NULL;
    *nFrames = 0;
    *nChannels = 0;
    *sr = 0;

    printf("[WAV] Loading file: %s\n", p);

    FILE* f = NULL;

    // Open WAV file in binary mode.
    if (fopen_s(&f, p, "rb") != 0 || !f) {
        fprintf(stderr, "[WAV] ERROR: Could not open file: %s\n", p);
        return false;
    }

    printf("[WAV] File opened successfully.\n");

    // Read the fixed WAV header.
    WavHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) {
        fprintf(stderr, "[WAV] ERROR: Failed to read WAV header.\n");
        fclose(f);
        return false;
    }

    printf("[WAV] Header read successfully.\n");

    // Validate RIFF/WAVE identifiers and format.
    // This loader only supports:
    // - RIFF WAVE files
    // - IEEE float format, format code 3
    // - 32-bit float samples
    // - one or more channels
    if (memcmp(h.riff, "RIFF", 4) ||
        memcmp(h.wave, "WAVE", 4) ||
        memcmp(h.fmt_, "fmt ", 4) ||
        h.format != 3 ||
        h.channels < 1 ||
        h.bitsPerSample != 32) {

        fprintf(stderr, "[WAV] ERROR: Unsupported WAV format.\n");
        fprintf(stderr, "[WAV] Expected: RIFF/WAVE, format=3 IEEE float, bits=32, channels>=1\n");
        fprintf(stderr, "[WAV] Found: format=%u, channels=%u, sampleRate=%u, bitsPerSample=%u\n",
            h.format,
            h.channels,
            h.sampleRate,
            h.bitsPerSample
        );

        fclose(f);
        return false;
    }

    printf("[WAV] Format validation passed.\n");
    printf("[WAV] Channels: %u\n", h.channels);
    printf("[WAV] Sample rate: %u\n", h.sampleRate);
    printf("[WAV] Bits per sample: %u\n", h.bitsPerSample);
    printf("[WAV] Format: %u, IEEE float expected value is 3\n", h.format);

    // If the fmt chunk contains extension bytes, skip them.
    // Your WavHeader struct already read the standard first 16 bytes of fmt data.
    if (h.fmtSize > 16) {
        unsigned int extraFmtBytes = h.fmtSize - 16;
        printf("[WAV] fmt chunk has %u extra bytes. Skipping them.\n", extraFmtBytes);

        if (fseek(f, extraFmtBytes, SEEK_CUR) != 0) {
            fprintf(stderr, "[WAV] ERROR: Failed to skip extra fmt bytes.\n");
            fclose(f);
            return false;
        }
    }

    char tag[4];
    unsigned int sz = 0;
    bool foundData = false;

    printf("[WAV] Searching for data chunk...\n");

    // Search for the "data" chunk.
    // WAV files can contain chunks between "fmt " and "data",
    // for example "LIST", "fact", etc.
    while (fread(tag, 1, 4, f) == 4) {

        if (fread(&sz, 4, 1, f) != 1) {
            fprintf(stderr, "[WAV] ERROR: Failed to read chunk size.\n");
            fclose(f);
            return false;
        }

        printf("[WAV] Found chunk: %.4s, size: %u bytes\n", tag, sz);

        // Stop when we find the audio data chunk.
        if (!memcmp(tag, "data", 4)) {
            foundData = true;
            printf("[WAV] data chunk found. Size: %u bytes\n", sz);
            break;
        }

        // Skip unknown/non-audio chunk.
        printf("[WAV] Skipping chunk: %.4s\n", tag);

        if (fseek(f, sz, SEEK_CUR) != 0) {
            fprintf(stderr, "[WAV] ERROR: Failed to skip chunk %.4s.\n", tag);
            fclose(f);
            return false;
        }

        // WAV chunks are word-aligned.
        // If a chunk has odd size, there is one padding byte after it.
        if (sz & 1) {
            printf("[WAV] Chunk has odd size. Skipping padding byte.\n");

            if (fseek(f, 1, SEEK_CUR) != 0) {
                fprintf(stderr, "[WAV] ERROR: Failed to skip padding byte.\n");
                fclose(f);
                return false;
            }
        }
    }

    if (!foundData) {
        fprintf(stderr, "[WAV] ERROR: data chunk not found.\n");
        fclose(f);
        return false;
    }

    int channels = (int)h.channels;
    int totalSamples = sz / sizeof(float);

    printf("[WAV] Total float samples in data chunk: %d\n", totalSamples);

    // Validate that the number of samples can be evenly divided
    // by the number of channels.
    //
    // Example stereo:
    // totalSamples = frames * 2
    if (totalSamples <= 0 || totalSamples % channels != 0) {
        fprintf(stderr, "[WAV] ERROR: Invalid sample count.\n");
        fprintf(stderr, "[WAV] totalSamples=%d, channels=%d\n", totalSamples, channels);
        fclose(f);
        return false;
    }

    int frames = totalSamples / channels;

    printf("[WAV] Frames: %d\n", frames);
    printf("[WAV] Channels: %d\n", channels);

    // Allocate temporary buffer for the raw WAV data.
    // WAV stores multichannel samples interleaved:
    //
    // frame 0: ch0, ch1, ch2, ...
    // frame 1: ch0, ch1, ch2, ...
    float* interleaved = (float*)malloc(sz);
    if (!interleaved) {
        fprintf(stderr, "[WAV] ERROR: Failed to allocate interleaved sample buffer.\n");
        fclose(f);
        return false;
    }

    printf("[WAV] Allocated interleaved buffer: %u bytes\n", sz);

    // Read all audio samples from the data chunk.
    if (fread(interleaved, sizeof(float), totalSamples, f) != (size_t)totalSamples) {
        fprintf(stderr, "[WAV] ERROR: Failed to read audio sample data.\n");
        free(interleaved);
        fclose(f);
        return false;
    }

    printf("[WAV] Audio sample data read successfully.\n");

    fclose(f);
    printf("[WAV] File closed.\n");

    // Allocate array of channel pointers.
    //
    // Output layout will be:
    //
    // out[0] -> all samples for channel 0
    // out[1] -> all samples for channel 1
    // out[2] -> all samples for channel 2
    // ...
    float** out = (float**)malloc(sizeof(float*) * channels);
    if (!out) {
        fprintf(stderr, "[WAV] ERROR: Failed to allocate channel pointer array.\n");
        free(interleaved);
        return false;
    }

    printf("[WAV] Allocated channel pointer array for %d channels.\n", channels);

    // Allocate one float array per channel.
    for (int c = 0; c < channels; ++c) {
        out[c] = (float*)malloc(sizeof(float) * frames);

        if (!out[c]) {
            fprintf(stderr, "[WAV] ERROR: Failed to allocate buffer for channel %d.\n", c);

            // Free any channel buffers that were already allocated.
            for (int k = 0; k < c; ++k) {
                free(out[k]);
            }

            free(out);
            free(interleaved);
            return false;
        }

        printf("[WAV] Allocated buffer for channel %d: %zu bytes\n",
            c,
            sizeof(float) * (size_t)frames
        );
    }

    printf("[WAV] Deinterleaving audio data...\n");

    // Deinterleave audio data.
    //
    // Input WAV layout:
    //
    // interleaved[0] = frame 0, channel 0
    // interleaved[1] = frame 0, channel 1
    // interleaved[2] = frame 0, channel 2
    // ...
    //
    // Output layout:
    //
    // out[channel][frame]
    //
    // Example:
    //
    // out[0][i] = sample of channel 0 at frame i
    // out[1][i] = sample of channel 1 at frame i
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            out[c][i] = interleaved[i * channels + c];
        }
    }

    printf("[WAV] Deinterleaving complete.\n");

    // Temporary interleaved buffer is no longer needed.
    free(interleaved);

    // Return output values to caller.
    *samples = out;
    *nFrames = frames;
    *nChannels = channels;
    *sr = h.sampleRate;

    printf("[WAV] Load successful.\n");
    printf("[WAV] Output: frames=%d, channels=%d, sampleRate=%d\n",
        *nFrames,
        *nChannels,
        *sr
    );

    return true;
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

std::string formatDurationHHMMSSMS(std::chrono::milliseconds duration)
{
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;

    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= minutes;

    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    duration -= seconds;

    auto milliseconds = duration;

    std::ostringstream oss;

    oss << std::setfill('0')
        << std::setw(2) << hours.count() << ":"
        << std::setw(2) << minutes.count() << ":"
        << std::setw(2) << seconds.count() << ":"
        << std::setw(3) << milliseconds.count();

    return oss.str();
}

int main()
{
	const char* output_dir = ".\\media";

	if (!create_output_directory(output_dir)) {
		std::cerr << "Failed to create output directory: " << output_dir << std::endl;
		return 1;
	}
    float* mono = NULL;
    float** samples = NULL;
	int total = 0;
	int sampleRate = 0;
	int nChannels = 1;


	std::string filename = "jazz";
	//std::string filename = "LinkinPark_InTheEnd";
	
    auto start = std::chrono::high_resolution_clock::now();
	//if (!load_wav_mono_f32((std::string(output_dir) + "\\" + filename + ".wav").c_str(), &mono, &total, &sampleRate)) { fprintf(stderr, "Expected mono 32-bit float WAV.\n"); return 1; }
	if (!load_wav_multichannel_f32((std::string(output_dir) + "\\" + filename + ".wav").c_str(), &samples, &total, &nChannels, &sampleRate)) { fprintf(stderr, "Expected mono multichannel 32-bit float WAV.\n"); return 1; }
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start
    );

    std::cout << "Elapsed time for wav reading: " << formatDurationHHMMSSMS(elapsed) << std::endl;

    std::vector<PlacementPosition> positions;
    int numSources = 1;

    std::string placementPath = std::string(output_dir) + "\\placement.json";
    bool hasPlacements = true;
    start = std::chrono::high_resolution_clock::now();
    if (!loadPlacementPositions(placementPath.c_str(), positions))
    {
        std::cerr << "Failed to load placement positions." << std::endl;
		hasPlacements = false;
    }
    end = std::chrono::high_resolution_clock::now();
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start
    );
    std::cout << "Elapsed time for json placing parse: " << formatDurationHHMMSSMS(elapsed) << std::endl;

	numSources = hasPlacements ? positions.size() : 1;

	Vec3 offset = { 0.0f, 1.5f, -1.5f };

    SteamAudioProcess sap = SteamAudioProcess();
	sap.numSources = numSources;
    for (int c = 0; c < numSources; ++c)
    {
	    float* sourceBuffer = (float*)calloc(sap.frameSize, sizeof(float));
	    sap.sourcesData.push_back(sourceBuffer);
        if (hasPlacements)
            sap.sourcesPositions.push_back((std::vector<float>{positions[c].position.x + offset.x, positions[c].position.y + offset.y, positions[c].position.z + offset.z}));
        else // default
            sap.sourcesPositions.push_back(std::vector<float>{ 0.0f, 1.7f, -3.0f });
    }

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

    start = std::chrono::high_resolution_clock::now();

	int cursor = 0, fidx = 0;
	while (cursor < total)
	{
		size_t ofs = fidx * sap.frameSize;
        for (int i = 0;i < sap.frameSize;++i)
        {
			//sap.sourcesData[0][i] = (cursor + i < total) ? mono[cursor + i] : 0.0f;
            for (size_t s = 0; s < numSources; s++)
            {
				sap.sourcesData[s][i] = (cursor + i < total) ? samples[positions[s].channelIndex][cursor + i] : 0.0f;
            }
        }

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

        if (fidx == 0)
        {
            end = std::chrono::high_resolution_clock::now();
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start
            );
			std::cout << "Elapsed time for first frame processing: " << formatDurationHHMMSSMS(elapsed) << std::endl;
        }
		fidx++;
	}

    end = std::chrono::high_resolution_clock::now();
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start
	);
	std::cout << "Elapsed time for all frames processing: " << formatDurationHHMMSSMS(elapsed) << std::endl;

	start = std::chrono::high_resolution_clock::now();
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_direct.wav").c_str(), DL, DR, bufferSize, sampleRate);
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_early.wav").c_str(), EL, ER, bufferSize, sampleRate);
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_reverb.wav").c_str(), RL, RR, bufferSize, sampleRate);
	end = std::chrono::high_resolution_clock::now();
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start
	);
	std::cout << "Elapsed time for wav writing: " << formatDurationHHMMSSMS(elapsed) << std::endl;
	
    sap.onDisable();
	sap.onDestroy();

    if (mono)
	    free(mono);

    if (samples)
    {
        for (int c = 0; c < nChannels; ++c) {
            free(samples[c]);
        }
        free(samples);
    }

    free(DL); free(DR); free(EL); free(ER); free(RL); free(RR);

	return 0;
}