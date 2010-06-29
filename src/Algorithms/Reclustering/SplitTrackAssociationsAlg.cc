/**
 *  @file   PandoraPFANew/src/Algorithms/Reclustering/SplitTrackAssociationsAlg.cc
 * 
 *  @brief  Implementation of the split track associations algorithm class.
 * 
 *  $Log: $
 */

#include "Algorithms/Reclustering/SplitTrackAssociationsAlg.h"

#include "Pandora/AlgorithmHeaders.h"

using namespace pandora;

StatusCode SplitTrackAssociationsAlg::Run()
{
    // Begin by recalculating track-cluster associations
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::RunDaughterAlgorithm(*this, m_trackClusterAssociationAlgName));

    // Store copy of input cluster list in a vector
    const ClusterList *pClusterList = NULL;
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentClusterList(*this, pClusterList));

    ClusterVector clusterVector(pClusterList->begin(), pClusterList->end());
    std::sort(clusterVector.begin(), clusterVector.end(), Cluster::SortByInnerLayer);

    // Examine each cluster in the input list
    for (ClusterVector::iterator iter = clusterVector.begin(), iterEnd = clusterVector.end(); iter != iterEnd; ++iter)
    {
        Cluster *pCluster = *iter;

        // Check compatibility of cluster with its associated tracks
        const TrackList &trackList(pCluster->GetAssociatedTrackList());
        const unsigned int nTrackAssociations(trackList.size());

        if ((nTrackAssociations < m_minTrackAssociations) || (nTrackAssociations > m_maxTrackAssociations))
            continue;

        const float chi(ReclusterHelper::GetTrackClusterCompatibility(pCluster, trackList));

        if (chi < m_chiToAttemptReclustering)
            continue;

        // Specify clusters and tracks to be used in reclustering
        ClusterList reclusterClusterList;
        reclusterClusterList.insert(pCluster);

        TrackList reclusterTrackList(trackList.begin(), trackList.end());

        // Initialize reclustering with these local lists
        std::string originalClustersListName;
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::InitializeReclustering(*this, reclusterTrackList, 
            reclusterClusterList, originalClustersListName));

        // Run multiple clustering algorithms and identify the best cluster candidates produced
        float bestReclusterChi2(std::numeric_limits<float>::max());
        std::string bestReclusterListName(originalClustersListName);

        for (StringVector::const_iterator clusteringIter = m_clusteringAlgorithms.begin(), clusteringIterEnd = m_clusteringAlgorithms.end();
            clusteringIter != clusteringIterEnd; ++clusteringIter)
        {
            // Produce new cluster candidates
            std::string reclusterListName;
            const ClusterList *pReclusterList = NULL;
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::RunClusteringAlgorithm(*this, *clusteringIter, 
                pReclusterList, reclusterListName));

            if (pReclusterList->empty())
                continue;

            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::RunDaughterAlgorithm(*this, m_associationAlgorithmName));
            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::RunDaughterAlgorithm(*this, m_trackClusterAssociationAlgName));

            // Calculate figure of merit for recluster candidates. Label as best recluster candidates if applicable
            ReclusterHelper::ReclusterResult reclusterResult;

            if (STATUS_CODE_SUCCESS != ReclusterHelper::ExtractReclusterResults(pReclusterList, reclusterResult))
                continue;

            if (reclusterResult.GetMinTrackAssociationEnergy() < m_minClusterEnergyForTrackAssociation)
                continue;

            if (reclusterResult.GetNExcessTrackAssociations() != 0)
                continue;

            // Are recluster candidates good enough to justify replacing original clusters?
            const float reclusterChi2(reclusterResult.GetChi2PerDof());
            static const float minChi2(m_chiToAttemptReclustering * m_chiToAttemptReclustering);

            if ((bestReclusterChi2 - reclusterChi2 > m_minChi2Improvement) && (reclusterChi2 < minChi2))
            {
                bestReclusterChi2 = reclusterChi2;
                bestReclusterListName = reclusterListName;
            }

            // If chi2 is very good, stop the reclustering attempts
            if(bestReclusterChi2 < m_chi2ForAutomaticClusterSelection)
                break;

            // If using ordered algorithms, chi2 is good enough and things are getting worse, stop
            if (m_usingOrderedAlgorithms && (bestReclusterChi2 < m_bestChi2ForReclusterHalt) && (reclusterChi2 > m_currentChi2ForReclusterHalt))
                break;
        }

        // Choose the best recluster candidates, which may still be the originals
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::EndReclustering(*this, bestReclusterListName));

        // Original cluster may have been deleted: for safety, remove its address from ClusterVector
        (*iter) = NULL;
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode SplitTrackAssociationsAlg::ReadSettings(const TiXmlHandle xmlHandle)
{
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ProcessAlgorithmList(*this, xmlHandle, "clusteringAlgorithms",
        m_clusteringAlgorithms));

    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ProcessAlgorithm(*this, xmlHandle, "ClusterAssociation",
        m_associationAlgorithmName));

    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ProcessAlgorithm(*this, xmlHandle, "TrackClusterAssociation",
        m_trackClusterAssociationAlgName));

    m_minTrackAssociations = 2;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinTrackAssociations", m_minTrackAssociations));

    if (m_minTrackAssociations < 1)
        return STATUS_CODE_INVALID_PARAMETER;

    m_maxTrackAssociations = std::numeric_limits<unsigned int>::max();
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxTrackAssociations", m_maxTrackAssociations));

    m_chiToAttemptReclustering = -3.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ChiToAttemptReclustering", m_chiToAttemptReclustering));

    m_minChi2Improvement = 1.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinChi2Improvement", m_minChi2Improvement));

    m_minClusterEnergyForTrackAssociation = 0.1f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinClusterEnergyForTrackAssociation", m_minClusterEnergyForTrackAssociation));

    m_chi2ForAutomaticClusterSelection = 1.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "Chi2ForAutomaticClusterSelection", m_chi2ForAutomaticClusterSelection));

    m_usingOrderedAlgorithms = false;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "UsingOrderedAlgorithms", m_usingOrderedAlgorithms));

    m_bestChi2ForReclusterHalt = 4.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "BestChi2ForReclusterHalt", m_bestChi2ForReclusterHalt));

    m_currentChi2ForReclusterHalt = 16.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "CurrentChi2ForReclusterHalt", m_currentChi2ForReclusterHalt));

    return STATUS_CODE_SUCCESS;
}
