/**
 *  @file   PandoraPFANew/src/Algorithms/Cheating/PerfectClusteringAlgorithm.cc
 * 
 *  @brief  Implementation of the cheating clustering algorithm class
 * 
 *  $Log: $
 */

#include "Algorithms/Cheating/PerfectClusteringAlgorithm.h"

#include "Api/PandoraContentApi.h"

#include "Objects/CaloHit.h"
#include "Objects/MCParticle.h"

using namespace pandora;



//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode PerfectClusteringAlgorithm::Run()
{
    const TrackList *pTrackList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentTrackList(*this, pTrackList));

    const OrderedCaloHitList *pOrderedCaloHitList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentOrderedCaloHitList(*this, pOrderedCaloHitList));

    // match calohitvectors to their MCParticles
    std::map< MCParticle*, CaloHitVector* > hitsPerMcParticle;
    std::map< MCParticle*, CaloHitVector* >::iterator itHitsPerMcParticle;

    for( OrderedCaloHitList::const_iterator itLyr = pOrderedCaloHitList->begin(), itLyrEnd = pOrderedCaloHitList->end(); itLyr != itLyrEnd; itLyr++ )
    {
        // int pseudoLayer = itLyr->first;
        CaloHitList::iterator itCaloHit    = itLyr->second->begin();
        CaloHitList::iterator itCaloHitEnd = itLyr->second->end();

        CaloHitVector* currentHits = NULL;
        for( ; itCaloHit != itCaloHitEnd; itCaloHit++ )
        {
            CaloHit* pCaloHit = (*itCaloHit);

            // fetch the MCParticle
            MCParticle* mc = NULL; 
            pCaloHit->GetMCParticle( mc );
            if( mc == NULL ) continue; // has to be continue, since sometimes some CalorimeterHits don't have a MCParticle (e.g. noise)


            // some selection criteria possible
            if( !SelectCaloHitsOfMcParticleForClustering( mc ) )
                continue;


            // add hit to calohitvector
            itHitsPerMcParticle = hitsPerMcParticle.find( mc );
            if( itHitsPerMcParticle == hitsPerMcParticle.end() )
            {
                // if there is no calohitvector for the MCParticle yet, create one
                currentHits = new CaloHitVector();
                hitsPerMcParticle.insert( std::make_pair( mc, currentHits ) );
            }
            else
            {
                currentHits = itHitsPerMcParticle->second; // take the calohitvector corresponding to the MCParticle
            }

            currentHits->push_back( pCaloHit ); // add the calohit
        }
    }

    // create the clusters
    ClusterList clusterList;
    pandora::Cluster *pCluster;

    for( std::map< MCParticle*, CaloHitVector* >::iterator itCHList = hitsPerMcParticle.begin(), itCHListEnd = hitsPerMcParticle.end(); 
         itCHList != itCHListEnd; ++itCHList )
    {
        if( itCHList->first == NULL ) continue; // hits without MCParticle are not clustered
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::Cluster::Create(*this, itCHList->second, pCluster ));
        
        if( !clusterList.insert( pCluster ).second )
            return STATUS_CODE_FAILURE;

        // delete the created CaloHitVectors
        delete itCHList->second;
    }

    if( !m_clusterListName.empty() )
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::SaveClusterList(*this, m_clusterListName, clusterList ));

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode PerfectClusteringAlgorithm::ReadSettings(const TiXmlHandle xmlHandle)
{
    m_clusterListName = "";
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ReadValue(xmlHandle, "clusterListName", m_clusterListName));

    return STATUS_CODE_SUCCESS;
}



//------------------------------------------------------------------------------------------------------------------------------------------

bool PerfectClusteringAlgorithm::SelectCaloHitsOfMcParticleForClustering( MCParticle* pMcParticle ) const
{
    return true;
}

