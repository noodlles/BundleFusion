#pragma once
#ifndef SUBMAP_MANAGER_H
#define SUBMAP_MAnAGER_H

#include "SiftGPU/SIFTImageManager.h"
#include "CUDAImageManager.h"
#include "CUDACache.h"

#include "SiftGPU/CUDATimer.h"
#include "GlobalBundlingState.h"
#include "mLibCuda.h"

class SiftGPU;
class SiftMatchGPU;

extern "C" void updateTrajectoryCU(
	float4x4* d_globalTrajectory, unsigned int numGlobalTransforms,
	float4x4* d_completeTrajectory, unsigned int numCompleteTransforms,
	float4x4* d_localTrajectories, unsigned int numLocalTransformsPerTrajectory, unsigned int numLocalTrajectories,
	int* d_imageInvalidateList);

extern "C" void initNextGlobalTransformCU(
	float4x4* d_globalTrajectory, unsigned int numGlobalTransforms,
	float4x4* d_localTrajectories, unsigned int numLocalTransformsPerTrajectory);

class SubmapManager {
public:
	CUDACache* currentLocalCache;
	CUDACache* nextLocalCache;
	CUDACache* optLocalCache;
	CUDACache* globalCache;

	SIFTImageManager* currentLocal;
	SIFTImageManager* nextLocal;
	SIFTImageManager* optLocal;
	SIFTImageManager* global;

	float4x4* d_globalTrajectory;
	float4x4* d_completeTrajectory;
	float4x4* d_localTrajectories;

	float4x4*	 d_siftTrajectory; // frame-to-frame sift tracking for all frames in sequence

	SubmapManager();
	void init(unsigned int maxNumGlobalImages, unsigned int maxNumLocalImages, unsigned int maxNumKeysPerImage,
		unsigned int submapSize, const CUDAImageManager* imageManager, unsigned int numTotalFrames = (unsigned int)-1);

	void setTotalNumFrames(unsigned int n) {
		m_numTotalFrames = n;
	}

	~SubmapManager();

	float4x4* getLocalTrajectoryGPU(unsigned int localIdx) const {
		return d_localTrajectories + localIdx * (m_submapSize + 1);
	}

	// update complete trajectory with new global trajectory info
	void updateTrajectory(unsigned int curFrame) {
		MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_imageInvalidateList, m_invalidImagesList.data(), sizeof(int)*curFrame, cudaMemcpyHostToDevice));
			
		updateTrajectoryCU(d_globalTrajectory, global->getNumImages(),
			d_completeTrajectory, curFrame,
			d_localTrajectories, m_submapSize + 1, global->getNumImages(),
			d_imageInvalidateList);
	}

	void initializeNextGlobalTransform(bool useIdentity = false) {
		const unsigned int numGlobalFrames = global->getNumImages();
		MLIB_ASSERT(numGlobalFrames >= 1);
		if (useIdentity) {
			MLIB_CUDA_SAFE_CALL(cudaMemcpy(d_globalTrajectory + numGlobalFrames, d_globalTrajectory + numGlobalFrames - 1, sizeof(float4x4), cudaMemcpyDeviceToDevice));
		}
		else {
			initNextGlobalTransformCU(d_globalTrajectory, numGlobalFrames, d_localTrajectories, m_submapSize + 1);
		}
	}

	void invalidateImages(unsigned int startFrame, unsigned int endFrame = -1) {
		//std::cout << "invalidating images (" << startFrame << ", " << endFrame << ")" << std::endl;
		//getchar();

		if (endFrame == -1) m_invalidImagesList[startFrame] = 0;
		else {
			for (unsigned int i = startFrame; i < endFrame; i++)
				m_invalidImagesList[i] = 0;
		}
	}

	void switchLocal() {
		
		//optLocal->lock();	//wait until optimizer has released its lock on opt local

		//SIFTImageManager* oldCurrentLocal = currentLocal;
		//SIFTImageManager* oldOptLocal = optLocal;
		//SIFTImageManager* oldNextLocal = nextLocal;		
		//currentLocal = oldNextLocal;
		//optLocal = oldCurrentLocal;
		//nextLocal = oldOptLocal;


		//CUDACache* oldCurrentLocalCache = currentLocalCache;
		//CUDACache* oldOptLocalCache = optLocalCache;
		//CUDACache* oldNextLocalCache = nextLocalCache;
		//currentLocalCache = oldNextLocalCache;
		//optLocalCache = oldCurrentLocalCache;
		//nextLocalCache = oldOptLocalCache;

		//oldOptLocal->unlock();

		std::swap(currentLocal, nextLocal);
		std::swap(currentLocalCache, nextLocalCache);

	
	}

	void switchLocalAndFinishOpt() {

		//optLocal->lock();	//wait until optimizer has released its lock on opt local

		//SIFTImageManager* oldCurrentLocal = currentLocal;
		//SIFTImageManager* oldOptLocal = optLocal;
		//SIFTImageManager* oldNextLocal = nextLocal;
		//currentLocal = oldNextLocal;
		//optLocal = oldCurrentLocal;
		//nextLocal = oldOptLocal;


		//CUDACache* oldCurrentLocalCache = currentLocalCache;
		//CUDACache* oldOptLocalCache = optLocalCache;
		//CUDACache* oldNextLocalCache = nextLocalCache;
		//currentLocalCache = oldNextLocalCache;
		//optLocalCache = oldCurrentLocalCache;
		//nextLocalCache = oldOptLocalCache;

		//optLocal->reset();
		//optLocalCache->reset();

		//oldOptLocal->unlock();

		std::swap(currentLocal, nextLocal);
		std::swap(currentLocalCache, nextLocalCache);
		nextLocal->reset();
		nextLocalCache->reset();
	}


	void finishLocalOpt() {
		nextLocal->reset();
		nextLocalCache->reset();

		//optLocal->reset();
		//optLocalCache->reset();
	}

	bool isLastFrame(unsigned int curFrame) const { return (curFrame + 1) == m_numTotalFrames; }
	bool isLastLocalFrame(unsigned int curFrame) const { return (curFrame >= m_submapSize && (curFrame % m_submapSize) == 0); }
	unsigned int getCurrLocal(unsigned int curFrame) const {
		const unsigned int curLocalIdx = (curFrame + 1 == m_numTotalFrames && (curFrame % m_submapSize != 0)) ? (curFrame / m_submapSize) : (curFrame / m_submapSize) - 1; // adjust for endframe
		return curLocalIdx;
	}

	void computeCurrentSiftTransform(unsigned int frameIdx, unsigned int localFrameIdx, unsigned int lastValidCompleteTransform) {
		const std::vector<int>& validImages = currentLocal->getValidImages();
		if (validImages[localFrameIdx] == 0) {
			m_currIntegrateTransform[frameIdx].setZero(-std::numeric_limits<float>::infinity());
			assert(frameIdx > 0);
			cutilSafeCall(cudaMemcpy(d_siftTrajectory + frameIdx, d_siftTrajectory + frameIdx - 1, sizeof(float4x4), cudaMemcpyDeviceToDevice));
			//cutilSafeCall(cudaMemcpy(d_currIntegrateTransform + frameIdx, &m_currIntegrateTransform[frameIdx], sizeof(float4x4), cudaMemcpyHostToDevice)); //TODO this is for debug only
		}
		if (frameIdx > 0) {
			currentLocal->computeSiftTransformCU(d_completeTrajectory, lastValidCompleteTransform, d_siftTrajectory, frameIdx, localFrameIdx, d_currIntegrateTransform + frameIdx);
			cutilSafeCall(cudaMemcpy(&m_currIntegrateTransform[frameIdx], d_currIntegrateTransform + frameIdx, sizeof(float4x4), cudaMemcpyDeviceToHost));
		}
	}
	const mat4f& getCurrentIntegrateTransform(unsigned int frameIdx) const { return m_currIntegrateTransform[frameIdx]; }
	const std::vector<mat4f>& getAllIntegrateTransforms() const { return m_currIntegrateTransform; }

	//!!!TODO DEBUG HACK ONLY
	SiftGPU* getSiftDEBUG() { return m_sift; }
	SiftMatchGPU* getSiftMatcherDEBUG() { return m_siftMatcher; }
	//!!!

private:
	enum TYPE {
		LOCAL_CURRENT,
		LOCAL_NEXT,
		GLOBAL
	};
	std::pair<SIFTImageManager*, CUDACache*> SubmapManager::get(TYPE type);
	void SubmapManager::finish(TYPE type);

	void initSIFT(unsigned int widthSift, unsigned int heightSift);

	//*********** SIFT *******************
	SiftGPU*				m_sift;
	SiftMatchGPU*			m_siftMatcher;
	//************ SUBMAPS ********************
	std::mutex mutex_curLocal;
	std::mutex mutex_nextLocal;
	std::mutex mutex_global;
	//************************************

	std::vector<unsigned int>	m_invalidImagesList;
	int*						d_imageInvalidateList; // tmp for updateTrajectory //TODO just to update trajectory on CPU

	float4x4*					d_currIntegrateTransform;
	std::vector<mat4f>			m_currIntegrateTransform;

	unsigned int m_numTotalFrames;
	unsigned int m_submapSize;

};

#endif