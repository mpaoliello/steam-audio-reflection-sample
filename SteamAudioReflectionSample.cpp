#include <iostream>
#include <filesystem>
#include <string>
#include <phonon.h>
#include <numbers>
#include <direct.h>   // For _mkdir on Windows

#ifndef LOG_FIRST_N_FRAMES
#define LOG_FIRST_N_FRAMES 10
#endif

#pragma pack(push,1)
typedef struct {
	char riff[4]; unsigned int size; char wave[4];
	char fmt_[4]; unsigned int fmtSize; unsigned short format;
	unsigned short channels; unsigned int sampleRate;
	unsigned int byteRate; unsigned short blockAlign; unsigned short bitsPerSample;
} WavHeader;
#pragma pack(pop)

static inline float blockRMS(const float* x, size_t n) { double s = 0; for (size_t i = 0;i < n;++i) { s += (double)x[i] * (double)x[i]; } return (float)std::sqrt(s / (double)std::max<size_t>(1, n)); }

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

static int num_samples_for_sec(float sec, int sr) { return (int)floorf(sec * (float)sr + 0.5f); }

static IPLMaterial stone{ .absorption = {0.05f, 0.07f, 0.08f}, .scattering = 0.05f, .transmission = {0.015f, 0.002f, 0.001f} };
static IPLMaterial materials[] = { stone };

static IPLStaticMesh create_static_scene(float W, float H, float D, IPLScene scene, IPLStaticMesh* mesh)
{
	// Build 6 rectangular faces (two triangles each) for the box, centered at bottom origin:
	const float x0 = -W * 0.5f, x1 = W * 0.5f;
	const float y0 = 0.0f, y1 = H;
	const float z0 = -D * 0.5f, z1 = D * 0.5f;

	// 8 corners
	IPLVector3 v[8] = {
		{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}, // floor 0..3
		{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}  // ceil  4..7
	};
	// 12 triangles (floor, ceiling, 4 walls)
	IPLTriangle t[12] = {
		{0,1,2},{0,2,3},         // floor
		{4,7,6},{4,6,5},         // ceiling
		{0,4,5},{0,5,1},         // -Z wall
		{1,5,6},{1,6,2},         // +X wall
		{2,6,7},{2,7,3},         // +Z wall
		{3,7,4},{3,4,0}          // -X wall
	};
	IPLint32 matIdx[12]; for (int i = 0;i < 12;++i) matIdx[i] = 0;

	IPLStaticMeshSettings mset{};
	mset.numVertices = 8; mset.numTriangles = 12; mset.numMaterials = 1;
	mset.vertices = v; mset.triangles = t; mset.materialIndices = matIdx; mset.materials = materials;
	if (iplStaticMeshCreate(scene, &mset, mesh) != IPL_STATUS_SUCCESS) { fprintf(stderr, "StaticMeshCreate failed.\n"); return nullptr; }

	if (!mesh) {
		std::cerr << "Failed to create static mesh." << std::endl;
		return nullptr;
	}

	std::cout << "Successfully created a static mesh of a cube centered at the origin." << std::endl;

	iplStaticMeshAdd(*mesh, scene);
	iplSceneCommit(scene);
}

int main()
{
	const char* output_dir = ".\\media";

	if (!create_output_directory(output_dir)) {
		std::cerr << "Failed to create output directory: " << output_dir << std::endl;
		return 1;
	}

	// Input mono audio channel buffer
	float* mono;
	int sampleRate = 0;
	int total = 0;
	float irDuration = 2.0f;
	const int frame = 1024;

	const int ambiOrder = 2;
	const int ambiCh = (ambiOrder + 1) * (ambiOrder + 1);

	const float earlySeconds = 1.0f;
	int irTotal;
	int irEarly;

	const float roomWidth = 10.0f;
	const float roomHeight = 10.0f;
	const float roomDepth = 10.0f;

	std::vector<float> outputDirectAudioFrame = std::vector<float>(2 * frame);
	std::vector<float> outputEarlyReflectionAudioFrame = std::vector<float>(2 * frame);
	std::vector<float> outputLateReflectionAudioFrame = std::vector<float>(2 * frame);

	std::string filename = "jazz2";
	if (!load_wav_mono_f32((std::string(output_dir) + "\\" + filename + ".wav").c_str(), &mono, &total, &sampleRate)) { fprintf(stderr, "Expected mono 32-bit float WAV.\n"); return 1; }

	irTotal = num_samples_for_sec(irDuration, sampleRate);
	irEarly = num_samples_for_sec(earlySeconds, sampleRate);
	
    std::cout << "[Info] irTotal = " << irTotal << ", irEarly = " << irEarly << std::endl;
	IPLContextSettings ctxs{};
	ctxs.version = STEAMAUDIO_VERSION;
	IPLContext ctx = nullptr;
	if (iplContextCreate(&ctxs, &ctx) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplContextCreate failed");
		return 1;
	}

	IPLAudioSettings as{};
	as.samplingRate = sampleRate;
	as.frameSize = frame;

	IPLHRTFSettings hs{};
	hs.type = IPL_HRTFTYPE_DEFAULT;
	hs.volume = 1.0f;
	IPLHRTF hrtf = nullptr;
	if (iplHRTFCreate(ctx, &as, &hs, &hrtf) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplHRTFCreate failed");
		return 1;
	}

	// Create effects

	IPLDirectEffectSettings des{};
	des.numChannels = 1;
	IPLDirectEffect direct = nullptr;
	if (iplDirectEffectCreate(ctx, &as, &des, &direct) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplDirectEffectCreate failed");
		return 1;
	}

	IPLBinauralEffectSettings bs{};
	bs.hrtf = hrtf;
	IPLBinauralEffect bin = nullptr;
	if (iplBinauralEffectCreate(ctx, &as, &bs, &bin) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplBinauralEffectCreate failed");
		return 1;
	}

	IPLReflectionEffectSettings res{};
	res.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
	res.irSize = num_samples_for_sec(irDuration, sampleRate);
    std::cout << "[ReflectionEffectSettings] irSize = " << res.irSize << std::endl;
	res.numChannels = ambiCh;
	IPLReflectionEffect refl = nullptr;
	if (iplReflectionEffectCreate(ctx, &as, &res, &refl) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplReflectionEffectCreate failed");
		return 1;
	}

	IPLAmbisonicsDecodeEffectSettings ds{};
	ds.maxOrder = ambiOrder;
	ds.hrtf = hrtf;
	IPLSpeakerLayout layout{};
	layout.type = IPL_SPEAKERLAYOUTTYPE_STEREO; // target stereo (+ binaural switch)
	ds.speakerLayout = layout;
	IPLAmbisonicsDecodeEffect decode = nullptr;
	if (iplAmbisonicsDecodeEffectCreate(ctx, &as, &ds, &decode) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplAmbisonicsDecodeEffectCreate failed");
		return 1;
	}

	// Create a scene (a simple box here)

	IPLSceneSettings ss{};
	ss.type = IPL_SCENETYPE_DEFAULT;
	IPLScene scene = nullptr;
	if (iplSceneCreate(ctx, &ss, &scene) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplSceneCreate failed");
		return 1;
	}

	IPLStaticMesh mesh = NULL;
	create_static_scene(roomWidth, roomHeight, roomDepth, scene, &mesh);

	// Set up the simulator

	IPLSimulationSettings sims{};
	sims.flags = IPLSimulationFlags(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
	sims.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
	sims.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
	sims.sceneType = IPL_SCENETYPE_DEFAULT;
	sims.maxNumRays = 4096;
	sims.numDiffuseSamples = 1024;
	sims.maxNumSources = 8;
	sims.maxOrder = ambiOrder;
	sims.maxDuration = irDuration;
	sims.samplingRate = sampleRate;
	sims.frameSize = frame;
	sims.numThreads = 2;
	sims.maxNumOcclusionSamples = 16;

	IPLSimulator sim = nullptr;
	if (iplSimulatorCreate(ctx, &sims, &sim) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplSimulatorCreate failed");
		return 1;
	}
	iplSimulatorSetScene(sim, scene);
	iplSimulatorCommit(sim);

	// Source
	IPLSourceSettings sset{};
	sset.flags = sims.flags;
	IPLSource src = nullptr;
	if (iplSourceCreate(sim, &sset, &src) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplSourceCreate failed");
		return 1;
	}
	iplSourceAdd(src, sim);
	iplSimulatorCommit(sim);

	// Listener shared inputs
	IPLCoordinateSpace3 L{}; L.right = { 1,0,0 }; L.up = { 0,1,0 }; L.ahead = { 0,0,-1 }; L.origin = { 0.0f,1.7f,0.0f };
	IPLSimulationSharedInputs shared{};
	shared.listener = L;
	shared.numRays = sims.maxNumRays;
	shared.numBounces = 4;
	shared.duration = irDuration;
	shared.order = ambiOrder;
	shared.irradianceMinDistance = 1.0f;
	iplSimulatorSetSharedInputs(sim, sims.flags, &shared);

	// Buffers

	const int framesTotal = (total + frame - 1) / frame;
	const int bufferSize = framesTotal * frame;
	std::cout << "Processing " << total << " samples in " << framesTotal << " frames of " << frame << " samples each.\n";
	float* DL = (float*)calloc(bufferSize, sizeof(float)),
		* DR = (float*)calloc(bufferSize, sizeof(float)),
		* EL = (float*)calloc(bufferSize, sizeof(float)),
		* ER = (float*)calloc(bufferSize, sizeof(float)),
		* RL = (float*)calloc(bufferSize, sizeof(float)),
		* RR = (float*)calloc(bufferSize, sizeof(float));


	float* A0 = (float*)calloc(bufferSize, sizeof(float)),
		* A1 = (float*)calloc(bufferSize, sizeof(float)),
		* A2 = (float*)calloc(bufferSize, sizeof(float)),
		* A3 = (float*)calloc(bufferSize, sizeof(float));

	IPLAudioBuffer inMono{}, outDirectBuffer{}, outLateReflectionBuffer{}, outEarlyReflectionBuffer{};
	iplAudioBufferAllocate(ctx, 1, frame, &inMono);
	iplAudioBufferAllocate(ctx, 1, frame, &outDirectBuffer); // 1 channel for direct sound
	iplAudioBufferAllocate(ctx, ambiCh, frame, &outEarlyReflectionBuffer); // 4 channels for 1st order Ambisonics
	iplAudioBufferAllocate(ctx, ambiCh, frame, &outLateReflectionBuffer); // 4 channels for 1st order Ambisonics
	IPLAudioBuffer outBinauralBuffer{}, outEarlyAmbisonicDecodeBuffer{}, outLateAmbisonicDecodeBuffer{};
	iplAudioBufferAllocate(ctx, 2, frame, &outBinauralBuffer);
	iplAudioBufferAllocate(ctx, 2, frame, &outEarlyAmbisonicDecodeBuffer);
	iplAudioBufferAllocate(ctx, 2, frame, &outLateAmbisonicDecodeBuffer);

	IPLAmbisonicsDecodeEffectParams dpar{}; dpar.order = ambiOrder; dpar.hrtf = hrtf; dpar.orientation = L; dpar.binaural = IPL_TRUE;

	IPLCoordinateSpace3 S{}; S.right = { 1,0,0 }; S.up = { 0,1,0 }; S.ahead = { 0,0,-1 }; S.origin = { 0.0f,L.origin.y, -2.0f };
	IPLVector3 dir = { S.origin.x - L.origin.x, S.origin.y - L.origin.y, S.origin.z - L.origin.z };

	int cursor = 0, fidx = 0;
	while (cursor < total)
	{
		size_t ofs = fidx * frame;
		size_t n = std::min((size_t)frame, total - ofs);
		// fill input frame
		for (int i = 0;i < frame;++i)
			inMono.data[0][i] = (cursor + i < total) ? mono[cursor + i] : 0.0f;
		if (fidx < LOG_FIRST_N_FRAMES) { std::printf("[Frame %zu/%zu]\n", fidx, framesTotal); }

		IPLSimulationInputs inps{};
		inps.flags = sims.flags;
		inps.directFlags = IPLDirectSimulationFlags(
			IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION
			| IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION
			//| IPL_DIRECTSIMULATIONFLAGS_OCCLUSION
			//| IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION
			);
		inps.source = S;
		inps.reverbScale[0] = inps.reverbScale[1] = inps.reverbScale[2] = 1.0f;
		inps.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
		inps.numOcclusionSamples = 16;

		// Increase for stronger early reflections; costs more CPU.
		inps.hybridReverbTransitionTime = earlySeconds;
		// e.g., 0.20f -> 20% of 100 ms = 20 ms crossfade
		float overlapFrac = 0.25f;
		inps.hybridReverbOverlapPercent = overlapFrac;

		iplSourceSetInputs(src, inps.flags, &inps);
		iplSimulatorCommit(sim);
		iplSimulatorRunDirect(sim);
		iplSimulatorRunReflections(sim);

		IPLSimulationOutputs so{};
		iplSourceGetOutputs(src, inps.flags, &so);
		if (fidx < LOG_FIRST_N_FRAMES) { std::printf("[Frame %zu] Reflections: ch=%d, irSize=%d, delay=%d, RT60={%.2f, %.2f, %.2f}, EQ={%.2f, %.2f, %.2f}\n", fidx, so.reflections.numChannels, so.reflections.irSize, so.reflections.delay, so.reflections.reverbTimes[0], so.reflections.reverbTimes[1], so.reflections.reverbTimes[2], so.reflections.eq[0], so.reflections.eq[1], so.reflections.eq[2]); }

		// DIRECT
		IPLDirectEffectParams dp{};
		dp.flags = IPLDirectEffectFlags(IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION /* | IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION*/);

		IPLDistanceAttenuationModel distanceAttenuationModel{};
		distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
		float distanceAttenuation = iplDistanceAttenuationCalculate(ctx, S.origin, L.origin, &distanceAttenuationModel);
		dp.distanceAttenuation = distanceAttenuation;


		// Log both distanceAttenuation and dp.distanceAttenuation
		if (fidx < LOG_FIRST_N_FRAMES) {
			std::printf("[Frame %zu] dp.distanceAttenuation = %.6f\n",
				fidx,
				dp.distanceAttenuation);
		}

		IPLAirAbsorptionModel airAbsorptionModel{};
		airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
		float airAbsorption[3];
		iplAirAbsorptionCalculate(ctx, S.origin, L.origin, &airAbsorptionModel, airAbsorption);
		dp.airAbsorption[0] = airAbsorption[0];
		dp.airAbsorption[1] = airAbsorption[1];
		dp.airAbsorption[2] = airAbsorption[2];

		if (fidx < LOG_FIRST_N_FRAMES) {
			std::printf("[Frame %zu] dp.airAbsorption = {%.6f, %.6f, %.6f}\n",
				fidx,
				dp.airAbsorption[0], dp.airAbsorption[1], dp.airAbsorption[2]);
		}

		dp.occlusion = 1;
		dp.transmission[0] = 1;
		dp.transmission[1] = 1;
		dp.transmission[2] = 1;

		// Log occlusion and transmission values
		if (fidx < LOG_FIRST_N_FRAMES) {
			std::printf("[Frame %zu] dp.occlusion = %.6f\n", fidx, dp.occlusion);
			std::printf("[Frame %zu] dp.transmission = {%.6f, %.6f, %.6f}\n",
				fidx,
				dp.transmission[0], dp.transmission[1], dp.transmission[2]);
		}

		iplDirectEffectApply(direct, &dp, &inMono, &outDirectBuffer);

		IPLBinauralEffectParams bp{};
		bp.hrtf = hrtf;
		bp.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		bp.spatialBlend = 1.0f;
		bp.direction = dir;
		iplBinauralEffectApply(bin, &bp, &outDirectBuffer, &outBinauralBuffer);

		// REFLECTIONS
		float rt60[3] = { so.reflections.reverbTimes[0], so.reflections.reverbTimes[1], so.reflections.reverbTimes[2] };
		float eq3[3] = { so.reflections.eq[0], so.reflections.eq[1], so.reflections.eq[2] };

		if (fidx < LOG_FIRST_N_FRAMES) std::printf("   IR sizes: full=%d, early=%d\n", so.reflections.irSize, irEarly);

		// (A) EARLY-ONLY: 
		{
			IPLReflectionEffectParams rp{};
			rp.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
			rp.ir = so.reflections.ir;
			rp.numChannels = so.reflections.numChannels;
			// no late tail
			rp.reverbTimes[0] = rp.reverbTimes[1] = rp.reverbTimes[2] = 0.0f;
			// use only early IR
			rp.irSize = irEarly;
			rp.delay = irEarly;
			// Apply to mono input -> Ambisonics buffer
			iplReflectionEffectApply(refl, &rp, &inMono, &outEarlyReflectionBuffer, nullptr);
			// Ambisonics decode -> stereo binaural
			iplAmbisonicsDecodeEffectApply(decode, &dpar, &outEarlyReflectionBuffer, &outEarlyAmbisonicDecodeBuffer);
		}

		// (B) LATE-ONLY: 
		{
			IPLReflectionEffectParams rp{};
			rp.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
			rp.ir = so.reflections.ir;
			rp.numChannels = so.reflections.numChannels;
			rp.delay = irEarly; // delay late tail to start after early IR
			// Setting irSize = 0 disables convolution entirely for that pass, only the parametric late tail is generated.
			rp.irSize = 0;
			rp.reverbTimes[0] = rt60[0];
			rp.reverbTimes[1] = rt60[1];
			rp.reverbTimes[2] = rt60[2];
			rp.eq[0] = eq3[0];
			rp.eq[1] = eq3[1];
			rp.eq[2] = eq3[2];

			// Apply to mono input -> Ambisonics buffer
			iplReflectionEffectApply(refl, &rp, &inMono, &outLateReflectionBuffer, nullptr);
			// Ambisonics decode -> stereo binaural
			iplAmbisonicsDecodeEffectApply(decode, &dpar, &outLateReflectionBuffer, &outLateAmbisonicDecodeBuffer);
		}

		// accumulate
		for (int i = 0;i < frame;++i) {
			int idx = fidx * frame + i;
			DL[idx] += outBinauralBuffer.data[0][i]; DR[idx] += outBinauralBuffer.data[1][i];
			EL[idx] += outEarlyAmbisonicDecodeBuffer.data[0][i];  ER[idx] += outEarlyAmbisonicDecodeBuffer.data[1][i];
			RL[idx] += outLateAmbisonicDecodeBuffer.data[0][i]; RR[idx] += outLateAmbisonicDecodeBuffer.data[1][i];
		}

		cursor += as.frameSize;
		fidx++;
	}

	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_direct.wav").c_str(), DL, DR, bufferSize, sampleRate);
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_early.wav").c_str(), EL, ER, bufferSize, sampleRate);
	write_wav_stereo_f32((std::string(output_dir) + "\\" + filename + "_reverb.wav").c_str(), RL, RR, bufferSize, sampleRate);

	// cleanup
	iplAudioBufferFree(ctx, &inMono);
	iplAudioBufferFree(ctx, &outDirectBuffer);
	iplAudioBufferFree(ctx, &outEarlyReflectionBuffer);
	iplAudioBufferFree(ctx, &outLateReflectionBuffer);
	iplAudioBufferFree(ctx, &outBinauralBuffer);
	iplAudioBufferFree(ctx, &outEarlyAmbisonicDecodeBuffer);
	iplAudioBufferFree(ctx, &outLateAmbisonicDecodeBuffer);
	iplReflectionEffectRelease(&refl);
	iplDirectEffectRelease(&direct);
	iplAmbisonicsDecodeEffectRelease(&decode);
	iplBinauralEffectRelease(&bin);
	iplSourceRemove(src, sim);
	iplSourceRelease(&src);
	iplStaticMeshRemove(mesh, scene);
	iplStaticMeshRelease(&mesh);
	iplSceneRelease(&scene);
	iplSimulatorRelease(&sim);
	iplHRTFRelease(&hrtf);
	iplContextRelease(&ctx);
	free(mono); free(DL); free(DR); free(EL); free(ER); free(RL); free(RR);

	return 0;
}