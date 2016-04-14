/**
 *  @file   larpandora/LArPandoraInterface/LArPandoraOutput.cxx
 *
 *  @brief  Helper functions for processing outputs from pandora
 *
 */

#include "art/Framework/Core/EDProducer.h"
#include "cetlib/exception.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include "larreco/ClusterFinder/ClusterCreator.h"
#include "larcore/Geometry/Geometry.h"
#include "larreco/RecoAlg/ClusterRecoUtil/StandardClusterParamsAlg.h"
#include "larreco/RecoAlg/ClusterParamsImportWrapper.h"
#include "lardata/Utilities/AssociationUtil.h"

#include "lardata/RecoBase/Hit.h"
#include "lardata/RecoBase/PFParticle.h"
#include "lardata/RecoBase/Seed.h"
#include "lardata/RecoBase/Shower.h"
#include "lardata/RecoBase/SpacePoint.h"
#include "lardata/RecoBase/Vertex.h"

#include "Objects/ParticleFlowObject.h"
#include "Objects/TrackState.h"
#include "Objects/Vertex.h"

#include "LArHelpers/LArClusterHelper.h"
#include "LArHelpers/LArPfoHelper.h"
#include "LArStitching/MultiPandoraApi.h"

#include "larpandora/LArPandoraInterface/LArPandoraOutput.h"

#include <algorithm>
#include <iterator>
#include <iostream>
#include <limits>

namespace lar_pandora
{

void LArPandoraOutput::ProduceArtOutput(const Settings &settings, const IdToHitMap &idToHitMap, art::Event &evt)
{
    mf::LogDebug("LArPandora") << " *** LArPandora::ProduceArtOutput() *** " << std::endl;

    if (!settings.m_pPrimaryPandora || !settings.m_pProducer)
        throw pandora::StatusCodeException(pandora::STATUS_CODE_INVALID_PARAMETER);

    PandoraInstanceList pandoraInstanceList;
    const PandoraInstanceList &daughterInstances(MultiPandoraApi::GetDaughterPandoraInstanceList(settings.m_pPrimaryPandora));

    if (settings.m_buildStitchedParticles || daughterInstances.empty())
        pandoraInstanceList.push_back(settings.m_pPrimaryPandora);

    if (settings.m_buildSingleVolumeParticles)
        pandoraInstanceList.insert(pandoraInstanceList.end(), daughterInstances.begin(), daughterInstances.end());

    pandora::PfoList concatenatedPfoList;
    for (const pandora::Pandora *const pPandora : pandoraInstanceList)
    {
        const pandora::PfoList *pPfoList(nullptr);
        PANDORA_THROW_RESULT_IF(pandora::STATUS_CODE_SUCCESS, !=, PandoraApi::GetCurrentPfoList(*pPandora, pPfoList));
        concatenatedPfoList.insert(pPfoList->begin(), pPfoList->end());
    }

    if (concatenatedPfoList.empty())
        mf::LogDebug("LArPandora") << "   Warning: No reconstructed particles for this event " << std::endl;

    // Set up ART outputs
    std::unique_ptr< std::vector<recob::PFParticle> > outputParticles( new std::vector<recob::PFParticle> );
    std::unique_ptr< std::vector<recob::SpacePoint> > outputSpacePoints( new std::vector<recob::SpacePoint> );
    std::unique_ptr< std::vector<recob::Cluster> >    outputClusters( new std::vector<recob::Cluster> );
    std::unique_ptr< std::vector<recob::Seed> >       outputSeeds( new std::vector<recob::Seed> );
    std::unique_ptr< std::vector<recob::Vertex> >     outputVertices( new std::vector<recob::Vertex> );
    std::unique_ptr< std::vector<recob::Track> >      outputTracks( new std::vector<recob::Track> );
    std::unique_ptr< std::vector<recob::Shower> >     outputShowers( new std::vector<recob::Shower> );

    std::unique_ptr< art::Assns<recob::PFParticle, recob::SpacePoint> > outputParticlesToSpacePoints( new art::Assns<recob::PFParticle, recob::SpacePoint> );
    std::unique_ptr< art::Assns<recob::PFParticle, recob::Cluster> >    outputParticlesToClusters( new art::Assns<recob::PFParticle, recob::Cluster> );
    std::unique_ptr< art::Assns<recob::PFParticle, recob::Seed> >       outputParticlesToSeeds( new art::Assns<recob::PFParticle, recob::Seed> );
    std::unique_ptr< art::Assns<recob::PFParticle, recob::Vertex> >     outputParticlesToVertices( new art::Assns<recob::PFParticle, recob::Vertex> );
    std::unique_ptr< art::Assns<recob::PFParticle, recob::Track> >      outputParticlesToTracks( new art::Assns<recob::PFParticle, recob::Track> );
    std::unique_ptr< art::Assns<recob::PFParticle, recob::Shower> >     outputParticlesToShowers( new art::Assns<recob::PFParticle, recob::Shower> );
    std::unique_ptr< art::Assns<recob::Track, recob::Hit> >             outputTracksToHits( new art::Assns<recob::Track, recob::Hit> );
    std::unique_ptr< art::Assns<recob::Shower, recob::Hit> >            outputShowersToHits( new art::Assns<recob::Shower, recob::Hit> );
    std::unique_ptr< art::Assns<recob::SpacePoint, recob::Hit> >        outputSpacePointsToHits( new art::Assns<recob::SpacePoint, recob::Hit> );
    std::unique_ptr< art::Assns<recob::Cluster, recob::Hit> >           outputClustersToHits( new art::Assns<recob::Cluster, recob::Hit> );
    std::unique_ptr< art::Assns<recob::Seed, recob::Hit> >              outputSeedsToHits( new art::Assns<recob::Seed, recob::Hit> );

    // prepare the algorithm to compute the cluster characteristics;
    // we use the "standard" one here; configuration would happen here,
    // but we are using the default configuration for that algorithm
    cluster::StandardClusterParamsAlg ClusterParamAlgo;
    
    // Obtain a sorted vector of all output Pfos and their daughters
    pandora::PfoList connectedPfoList;
    lar_content::LArPfoHelper::GetAllConnectedPfos(concatenatedPfoList, connectedPfoList);
    pandora::PfoVector pfoVector(connectedPfoList.begin(), connectedPfoList.end());
    std::sort(pfoVector.begin(), pfoVector.end(), lar_content::LArPfoHelper::SortByNHits);

    int vertexCounter(0);
    int spacePointCounter(0);
    int clusterCounter(0);
    int trackCounter(0);
    size_t particleCounter(0);

    // Build maps of Pandora particles and Pandora vertices
    pandora::VertexVector vertexVector;
    ThreeDParticleMap particleMap;
    ThreeDVertexMap vertexMap;

    for (pandora::PfoVector::iterator pIter = pfoVector.begin(), pIterEnd = pfoVector.end(); pIter != pIterEnd; ++pIter)
    {
        const pandora::ParticleFlowObject *const pPfo = *pIter;
        particleMap.insert( std::pair<const pandora::ParticleFlowObject*, size_t>(pPfo, particleCounter++) ); 

        if (!pPfo->GetVertexList().empty())
        {
            if(pPfo->GetVertexList().size() != 1)
                throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

            const pandora::Vertex *const pVertex = *(pPfo->GetVertexList().begin());

            if (vertexMap.end() != vertexMap.find(pVertex))
                continue;

            vertexVector.push_back(pVertex);
            vertexMap.insert( std::pair<const pandora::Vertex*, unsigned int>(pVertex, vertexVector.size() - 1) ); 
        }
    }

    // Loop over Pandora vertices and build recob::Vertices
    for (pandora::VertexVector::iterator vIter = vertexVector.begin(), vIterEnd = vertexVector.end(); vIter != vIterEnd; ++vIter)
    {
        const pandora::Vertex *const pVertex = *vIter;

        ThreeDVertexMap::const_iterator wIter = vertexMap.find(pVertex);
        if (vertexMap.end() == wIter)
            throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

        const pandora::CartesianVector vtxPos(pVertex->GetPosition());
        double pos[3] = { vtxPos.GetX(), vtxPos.GetY(), vtxPos.GetZ() };
        
        recob::Vertex newVertex(pos, vertexCounter++);
        outputVertices->push_back(newVertex);
    }

    // Loop over Pandora particles and build recob::PFParticles
    for (pandora::PfoVector::iterator pIter = pfoVector.begin(), pIterEnd = pfoVector.end(); pIter != pIterEnd; ++pIter)
    {
        const pandora::ParticleFlowObject *const pPfo = *pIter;

        // Get Pfo ID
        ThreeDParticleMap::const_iterator qIter = particleMap.find(pPfo);
        if (particleMap.end() == qIter)
            throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

        const size_t pfoIdCode(qIter->second);

        // Get Pfo Parents
        size_t parentIdCode(recob::PFParticle::kPFParticlePrimary);
        const pandora::PfoList &parentList(pPfo->GetParentPfoList());

        if (!parentList.empty())
        {
            if (parentList.size() != 1)
                throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

            for (pandora::PfoList::const_iterator parentIter = parentList.begin(), parentIterEnd = parentList.end();
                parentIter != parentIterEnd; ++parentIter)
            {
                const pandora::ParticleFlowObject *const pParentPfo = *parentIter;

                ThreeDParticleMap::const_iterator parentIdIter = particleMap.find(pParentPfo);
                if (particleMap.end() == parentIdIter)
                    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

                parentIdCode = parentIdIter->second;
            }
        }

        // Get Pfo Daughters
        std::vector<size_t> daughterIdCodes;
        const pandora::PfoList &daughterList(pPfo->GetDaughterPfoList());

        if (!daughterList.empty())
        {
            for (pandora::PfoList::const_iterator daughterIter = daughterList.begin(), daughterIterEnd = daughterList.end();
                daughterIter != daughterIterEnd; ++daughterIter)
            {
                const pandora::ParticleFlowObject *const pDaughterPfo = *daughterIter;

                ThreeDParticleMap::const_iterator daughterIdIter = particleMap.find(pDaughterPfo);
                if (particleMap.end() == daughterIdIter)
                    throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

                const size_t daughterIdCode(daughterIdIter->second);
                daughterIdCodes.push_back(daughterIdCode);
            }
        }

        // Build Particle
        recob::PFParticle newParticle(pPfo->GetParticleId(), pfoIdCode, parentIdCode, daughterIdCodes);
        outputParticles->push_back(newParticle);

        // Build 3D Space Points 
        pandora::CaloHitList pandoraHitList3D;
        lar_content::LArPfoHelper::GetCaloHits(pPfo, pandora::TPC_3D, pandoraHitList3D);
        pandora::CaloHitVector pandoraHitVector3D(pandoraHitList3D.begin(), pandoraHitList3D.end());
        std::sort(pandoraHitVector3D.begin(), pandoraHitVector3D.end(), lar_content::LArClusterHelper::SortByPosition);

        for (pandora::CaloHitVector::const_iterator hIter = pandoraHitVector3D.begin(), hIterEnd = pandoraHitVector3D.end(); hIter != hIterEnd; ++hIter)
        {
            const pandora::CaloHit *const pCaloHit3D = *hIter;

            if (pandora::TPC_3D != pCaloHit3D->GetHitType())
                throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

            const pandora::CaloHit *const pCaloHit2D = static_cast<const pandora::CaloHit*>(pCaloHit3D->GetParentCaloHitAddress());

            HitVector hitVector;
            const art::Ptr<recob::Hit> hit = LArPandoraOutput::GetHit(idToHitMap, pCaloHit2D);
            hitVector.push_back(hit);

            outputSpacePoints->emplace_back(LArPandoraOutput::BuildSpacePoint(spacePointCounter++, pCaloHit3D));

            util::CreateAssn(*(settings.m_pProducer), evt, *(outputSpacePoints.get()), hitVector, *(outputSpacePointsToHits.get()));
            util::CreateAssn(*(settings.m_pProducer), evt, *(outputParticles.get()), *(outputSpacePoints.get()), *(outputParticlesToSpacePoints.get()),
                outputSpacePoints->size() - 1, outputSpacePoints->size());
        }

        // Build 2D Clusters   
        pandora::ClusterVector pandoraClusterVector(pPfo->GetClusterList().begin(), pPfo->GetClusterList().end());
        std::sort(pandoraClusterVector.begin(), pandoraClusterVector.end(), lar_content::LArClusterHelper::SortByNHits);

        for (pandora::ClusterVector::const_iterator cIter = pandoraClusterVector.begin(), cIterEnd = pandoraClusterVector.end(); cIter != cIterEnd; ++cIter)
        {
            const pandora::Cluster *const pCluster = *cIter;

            if (pandora::TPC_3D == lar_content::LArClusterHelper::GetClusterHitType(pCluster))
                continue;

            pandora::CaloHitList pandoraHitList2D;
            pCluster->GetOrderedCaloHitList().GetCaloHitList(pandoraHitList2D);
            pandoraHitList2D.insert(pCluster->GetIsolatedCaloHitList().begin(), pCluster->GetIsolatedCaloHitList().end());
            pandora::CaloHitVector pandoraHitVector2D(pandoraHitList2D.begin(), pandoraHitList2D.end());
            std::sort(pandoraHitVector2D.begin(), pandoraHitVector2D.end(), lar_content::LArClusterHelper::SortByPosition);

            HitArray  hitArray;      // sort hits by drift volume
            HitList   isolatedHits;  // select isolated hits

            for (pandora::CaloHitVector::const_iterator hIter = pandoraHitVector2D.begin(), hIterEnd = pandoraHitVector2D.end(); hIter != hIterEnd; ++hIter)
            {
                const pandora::CaloHit *const pCaloHit2D = *hIter;
                const art::Ptr<recob::Hit> hit = LArPandoraOutput::GetHit(idToHitMap, pCaloHit2D);

                const geo::WireID wireID(hit->WireID());
                const unsigned int volID(100000 * wireID.Cryostat + wireID.TPC);

                hitArray[volID].push_back(hit);

                if (pCaloHit2D->IsIsolated())
                    isolatedHits.insert(hit);
            }

            if (hitArray.empty())
                throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

            for (HitArray::const_iterator hIter = hitArray.begin(), hIterEnd = hitArray.end(); hIter != hIterEnd; ++hIter)
            {
                const HitVector clusterHits(hIter->second);
                outputClusters->emplace_back(LArPandoraOutput::BuildCluster(clusterCounter++, clusterHits, isolatedHits, ClusterParamAlgo)); 

                util::CreateAssn(*(settings.m_pProducer), evt, *(outputClusters.get()), clusterHits, *(outputClustersToHits.get()));
                util::CreateAssn(*(settings.m_pProducer), evt, *(outputParticles.get()), *(outputClusters.get()), *(outputParticlesToClusters.get()),
                    outputClusters->size() - 1, outputClusters->size());
            }
        }

        // Associate Vertex and Build Seeds (and Tracks)
        if (!pPfo->GetVertexList().empty())
        {
            if(pPfo->GetVertexList().size() != 1)
                throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

            const pandora::Vertex *const pVertex = *(pPfo->GetVertexList().begin());

            ThreeDVertexMap::const_iterator vIter = vertexMap.find(pVertex);
            if (vertexMap.end() == vIter)
                throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

            const unsigned int vtxElement(vIter->second);

            util::CreateAssn(*(settings.m_pProducer), evt, *(outputParticles.get()), *(outputVertices.get()), *(outputParticlesToVertices.get()),
                vtxElement, vtxElement + 1);

            if (lar_content::LArPfoHelper::IsTrack(pPfo) && pPfo->GetMomentum().GetMagnitudeSquared() > std::numeric_limits<float>::epsilon())
            {
                try
		{
                    const lar_content::LArTrackPfo *const pLArTrackPfo = dynamic_cast<const lar_content::LArTrackPfo*>(pPfo);
        
                    if (!pLArTrackPfo)
                        throw cet::exception("LArPandora") << " LArPandoraOutput::BuildSeeds --- input pfo was not track-like ";
                
                    const lar_content::LArTrackStateVector &trackStateVector = pLArTrackPfo->m_trackStateVector;

                    if (trackStateVector.empty())
                        throw cet::exception("LArPandora") << " LArPandoraOutput::BuildTrack --- No input trajectory points were provided ";

                    HitVector trackHits;

                    for (lar_content::LArTrackStateVector::const_iterator tIter = trackStateVector.begin(), tIterEnd = trackStateVector.end();
                        tIter != tIterEnd; ++tIter)
		    {
                        const lar_content::LArTrackState &nextPoint = *tIter;
  
                        HitVector seedHits;
                        const art::Ptr<recob::Hit> hit = LArPandoraOutput::GetHit(idToHitMap, nextPoint.GetCaloHit());
                        seedHits.push_back(hit);
                        trackHits.push_back(hit);

                        outputSeeds->emplace_back(LArPandoraOutput::BuildSeed(nextPoint));
  
                        util::CreateAssn(*(settings.m_pProducer), evt, *(outputSeeds.get()), seedHits, *(outputSeedsToHits.get()));
                        util::CreateAssn(*(settings.m_pProducer), evt, *(outputParticles.get()), *(outputSeeds.get()), *(outputParticlesToSeeds.get()),
                            outputSeeds->size() - 1, outputSeeds->size());
		    }
	
                    if (settings.m_buildTracks)
                    {
                        outputTracks->emplace_back(LArPandoraOutput::BuildTrack(trackCounter++, &trackStateVector));

                        util::CreateAssn(*(settings.m_pProducer), evt, *(outputTracks.get()), trackHits, *(outputTracksToHits.get()));
                        util::CreateAssn(*(settings.m_pProducer), evt, *(outputParticles.get()), *(outputTracks.get()), *(outputParticlesToTracks.get()),
                            outputTracks->size() - 1, outputTracks->size());
                    }
		}
                catch (cet::exception &e)
                {
                }
            }
        }

        //
        // TODO: Build Showers here
        //
    }

    mf::LogDebug("LArPandora") << "   Number of new particles: " << outputParticles->size() << std::endl;
    mf::LogDebug("LArPandora") << "   Number of new clusters: " << outputClusters->size() << std::endl;
    mf::LogDebug("LArPandora") << "   Number of new space points: " << outputSpacePoints->size() << std::endl;
    mf::LogDebug("LArPandora") << "   Number of new seeds: " << outputSeeds->size() << std::endl;
    mf::LogDebug("LArPandora") << "   Number of new vertices: " << outputVertices->size() << std::endl;

    if (settings.m_buildTracks)
        mf::LogDebug("LArPandora") << "   Number of new tracks: " << outputTracks->size() << std::endl;

    if (settings.m_buildShowers)
        mf::LogDebug("LArPandora") << "   Number of new showers: " << outputShowers->size() << std::endl;

    evt.put(std::move(outputParticles));
    evt.put(std::move(outputSpacePoints));
    evt.put(std::move(outputClusters));
    evt.put(std::move(outputSeeds));
    evt.put(std::move(outputVertices));

    evt.put(std::move(outputParticlesToSpacePoints));
    evt.put(std::move(outputParticlesToClusters));
    evt.put(std::move(outputParticlesToSeeds));
    evt.put(std::move(outputParticlesToVertices));
    evt.put(std::move(outputSpacePointsToHits));
    evt.put(std::move(outputClustersToHits));
    evt.put(std::move(outputSeedsToHits));

    if (settings.m_buildTracks)
    {
        evt.put(std::move(outputTracks));
        evt.put(std::move(outputParticlesToTracks));
        evt.put(std::move(outputTracksToHits));
    }

    if (settings.m_buildShowers)
    {
        evt.put(std::move(outputShowers));
        evt.put(std::move(outputParticlesToShowers));
        evt.put(std::move(outputShowersToHits));
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
 
recob::Cluster LArPandoraOutput::BuildCluster(const int id, const HitVector &hitVector, const HitList &hitList, cluster::ClusterParamsAlgBase &algo) 
{
    mf::LogDebug("LArPandora") << "   Building Cluster [" << id << "], Number of hits = " << hitVector.size() << std::endl;

    if (hitVector.empty())
        throw cet::exception("LArPandora") << " LArPandoraOutput::BuildCluster --- No input hits were provided ";

    // Fill list of cluster properties
    geo::View_t view(geo::kUnknown);
    geo::PlaneID planeID;

    double startWire(+std::numeric_limits<float>::max()), sigmaStartWire(0.0);
    double startTime(+std::numeric_limits<float>::max()), sigmaStartTime(0.0);
    double endWire(-std::numeric_limits<float>::max()), sigmaEndWire(0.0);
    double endTime(-std::numeric_limits<float>::max()), sigmaEndTime(0.0);
    
    std::vector<recob::Hit const*> hits_for_params;
    hits_for_params.reserve(hitVector.size());
    
    for (std::vector<art::Ptr<recob::Hit>>::const_iterator iter = hitVector.begin(), iterEnd = hitVector.end(); iter != iterEnd; ++iter)
    {
        art::Ptr<recob::Hit> const& hit = *iter;
        
        const double thisWire(hit->WireID().Wire);
        const double thisWireSigma(0.5);
        const double thisTime(hit->PeakTime());
        const double thisTimeSigma(double(2.*hit->RMS()));
        const geo::View_t thisView(hit->View());
        const geo::PlaneID thisPlaneID(hit->WireID().planeID());

        if (geo::kUnknown == view)
        {
            view = thisView;
            planeID = thisPlaneID;
        }

        if (!(thisView == view && thisPlaneID == planeID))
        {
            throw cet::exception("LArPandora") << " LArPandoraOutput::BuildCluster --- Input hits have inconsistent plane IDs ";
        }
        
        hits_for_params.push_back(&*hit);
        
        if (hitList.count(hit))
            continue;

        if (thisWire < startWire || (thisWire == startWire && thisTime < startTime))
        {
            startWire = thisWire;
            sigmaStartWire = thisWireSigma;
            startTime = thisTime;
            sigmaStartTime = thisTimeSigma;
        }

        if (thisWire > endWire || (thisWire == endWire && thisTime > endTime))
        {
            endWire = thisWire;
            sigmaEndWire = thisWireSigma;
            endTime = thisTime;
            sigmaEndTime = thisTimeSigma;
        }

    }
    
    // feed the algorithm with all the cluster hits
    algo.SetHits(hits_for_params);
    
    // create the recob::Cluster directly in the vector
    return cluster::ClusterCreator(
      algo,                  // algo
      startWire,             // start_wire
      sigmaStartWire,        // sigma_start_wire
      startTime,             // start_tick
      sigmaStartTime,        // sigma_start_tick
      endWire,               // end_wire
      sigmaEndWire,          // sigma_end_wire
      endTime,               // end_tick
      sigmaEndTime,          // sigma_end_tick
      id,                    // ID
      view,                  // view
      planeID,               // plane
      recob::Cluster::Sentry // sentry
      ).move();
}

//------------------------------------------------------------------------------------------------------------------------------------------

recob::Seed LArPandoraOutput::BuildSeed(const lar_content::LArTrackState &trackState)
{
    double pos[3]     = { trackState.GetPosition().GetX(), trackState.GetPosition().GetY(), trackState.GetPosition().GetZ() };
    double dir[3]     = { trackState.GetDirection().GetX(), trackState.GetDirection().GetY(), trackState.GetDirection().GetZ() };
    double posErr[3]  = { 0.0, 0.0, 0.0 };  // TODO: Fill in errors
    double dirErr[3]  = { 0.0, 0.0, 0.0 };  // TODO: Fill in errors

    return recob::Seed(pos, dir, posErr, dirErr);
}

//------------------------------------------------------------------------------------------------------------------------------------------

recob::Track LArPandoraOutput::BuildTrack(const int id, const lar_content::LArTrackStateVector *const pTrackStateVector)
{
    mf::LogDebug("LArPandora") << "   Building Track [" << id << "], Number of trajectory points = " << pTrackStateVector->size() << std::endl;

    if (pTrackStateVector->empty())
        throw cet::exception("LArPandora") << " LArPandoraOutput::BuildTrack --- No input trajectory points were provided ";

    // Fill list of track properties
    std::vector<TVector3>               xyz;
    std::vector<TVector3>               pxpypz;
    std::vector< std::vector<double> >  dQdx(3);
    std::vector<double>                 momentum = std::vector<double>(2, util::kBogusD);

    // Loop over trajectory points
    for (lar_content::LArTrackStateVector::const_iterator tIter = pTrackStateVector->begin(), tIterEnd = pTrackStateVector->end();
        tIter != tIterEnd; ++tIter)
    {
        const lar_content::LArTrackState &nextPoint = *tIter;

        if (nextPoint.GetdQdL() < std::numeric_limits<float>::epsilon())
            continue;

        const float dQdxU((pandora::TPC_VIEW_U == nextPoint.GetHitType()) ? nextPoint.GetdQdL() : 0.f);
        const float dQdxV((pandora::TPC_VIEW_V == nextPoint.GetHitType()) ? nextPoint.GetdQdL() : 0.f);
        const float dQdxW((pandora::TPC_VIEW_W == nextPoint.GetHitType()) ? nextPoint.GetdQdL() : 0.f);

        xyz.push_back(TVector3(nextPoint.GetPosition().GetX(), nextPoint.GetPosition().GetY(), nextPoint.GetPosition().GetZ()));
        pxpypz.push_back(TVector3(nextPoint.GetDirection().GetX(), nextPoint.GetDirection().GetY(), nextPoint.GetDirection().GetZ()));
        dQdx.at(geo::kU).push_back(dQdxU); dQdx.at(geo::kV).push_back(dQdxV); dQdx.at(geo::kW).push_back(dQdxW);
    }

    // Return a new recob::Track object (of the Bezier variety)
    return recob::Track(xyz, pxpypz, dQdx, momentum, id);
}
  
//------------------------------------------------------------------------------------------------------------------------------------------

recob::SpacePoint LArPandoraOutput::BuildSpacePoint(const int id, const pandora::CaloHit *const pCaloHit)
{
    if (pandora::TPC_3D != pCaloHit->GetHitType())
        throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

    const pandora::CartesianVector point(pCaloHit->GetPositionVector());
    double xyz[3] = { point.GetX(), point.GetY(), point.GetZ() };
    double dxdydz[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }; // TODO: Fill in errors
    double chi2(0.0);

    return recob::SpacePoint(xyz, dxdydz, chi2, id);
}

//------------------------------------------------------------------------------------------------------------------------------------------

art::Ptr<recob::Hit> LArPandoraOutput::GetHit(const IdToHitMap &idToHitMap, const pandora::CaloHit *const pCaloHit)
{
    const void *const pHitAddress(pCaloHit->GetParentCaloHitAddress());
    const intptr_t hitID_temp((intptr_t)(pHitAddress)); // TODO
    const int hitID((int)(hitID_temp));

    IdToHitMap::const_iterator artIter = idToHitMap.find(hitID);

    if (idToHitMap.end() == artIter)
        throw pandora::StatusCodeException(pandora::STATUS_CODE_FAILURE);

    return artIter->second;
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArPandoraOutput::Settings::Settings() :
    m_pPrimaryPandora(nullptr),
    m_pProducer(nullptr),
    m_buildTracks(true),
    m_buildShowers(true),
    m_buildStitchedParticles(false),
    m_buildSingleVolumeParticles(true)
{
}

} // namespace lar_pandora