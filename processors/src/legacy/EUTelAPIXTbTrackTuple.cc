/*
 *   This source code is part of the Eutelescope package of Marlin.
 *   You are free to use this source files for your own development as
 *   long as it stays in a public research context. You are not
 *   allowed to use it for commercial purpose. You must put this
 *   header with author names in all development based on this file.
 *
 */
 
// eutelescope includes ".h"
#include "EUTelAPIXTbTrackTuple.h"
#include "EUTELESCOPE.h"
#include "EUTelEventImpl.h"
#include "EUTelExceptions.h"
#include "EUTelRunHeaderImpl.h"
#include "EUTelTrackerDataInterfacerImpl.h"
#include "EUTelGenericPixGeoDescr.h"
#include "EUTelGeometryTelescopeGeoDescription.h"

// lcio includes <.h>
#include <EVENT/LCCollection.h>
#include <EVENT/LCEvent.h>
#include <IMPL/LCCollectionVec.h>
#include <IMPL/TrackImpl.h>
#include <IMPL/TrackerHitImpl.h>
#include <UTIL/CellIDDecoder.h>

// system includes <>
#include <algorithm>

using namespace eutelescope;

EUTelAPIXTbTrackTuple::EUTelAPIXTbTrackTuple()
    : Processor("EUTelAPIXTbTrackTuple"), _inputTrackColName(""),
      _inputTrackerHitColName(""), _inputDutPulseCollectionName(""), _dutZsColName(""),
      _path2file(""), _DUTIDs(std::vector<int>()), _nRun(0), _nEvt(0),
      _runNr(0), _evtNr(0), _isFirstEvent(false), _file(nullptr), _eutracks(nullptr),
      _nTrackParams(0), _xPos(nullptr), _yPos(nullptr), _dxdz(nullptr), _dydz(nullptr),
      _trackIden(nullptr), _trackNum(nullptr), _chi2(nullptr), _ndof(nullptr),
      _zstree(nullptr), _nPixHits(0), p_col(nullptr), p_row(nullptr), p_tot(nullptr),
      p_iden(nullptr), p_lv1(nullptr), p_hitTime(nullptr), p_frameTime(nullptr),
      _euhits(nullptr), _nHits(0), _hitXPos(nullptr), _hitYPos(nullptr), _hitZPos(nullptr),
      _hitSensorId(nullptr) {
      
  // processor description
  _description = "EUTelAPIXTbTrackTuple prepares tbtrack style n-tuple with track fit results.";

  registerInputCollection(LCIO::TRACK,
			  "InputTrackCollectionName",
                          "Name of the input Track collection",
                          _inputTrackColName,
			  std::string("fittracks"));

  registerInputCollection(LCIO::TRACKERHIT,
			  "InputTrackerHitCollectionName",
                          "Name of the plane-wide hit-data hit collection",
                          _inputTrackerHitColName,
			  std::string("fitpoints"));

  registerProcessorParameter("DutZsColName",
                             "DUT zero surpressed data colection name",
                             _dutZsColName,
			     std::string("zsdata_apix"));

  registerProcessorParameter("OutputPath",
                             "Path/File where root-file should be stored",
                             _path2file,
			     std::string("NTuple.root"));

  registerProcessorParameter("DUTIDs",
                             "IDs of the DUTs",
                             _DUTIDs,
			     std::vector<int>());
}

void EUTelAPIXTbTrackTuple::init() {
  // usually a good idea to do
  printParameters();

  _isFirstEvent = true;

  _nRun = 0;
  _nEvt = 0;

  prepareTree();

  geo::gGeometry().initializeTGeoDescription(EUTELESCOPE::GEOFILENAME,
                                             EUTELESCOPE::DUMPGEOROOT);

  for (auto dutID : _DUTIDs) {
    // Later we need to shift the sensor since in EUTel centre of sensor is 0|0
    // while in TBmon(II) it is in the lower left corner
    geo::EUTelGenericPixGeoDescr *geoDescr =
        geo::gGeometry().getPixGeoDescr(dutID);
    double xSize, ySize;
    geoDescr->getSensitiveSize(xSize, ySize);

    _xSensSize[dutID] = xSize;
    _ySensSize[dutID] = ySize;
  }
}

void EUTelAPIXTbTrackTuple::processRunHeader(LCRunHeader *runHeader) {

  auto eutelHeader = std::make_unique<EUTelRunHeaderImpl>(runHeader);
  eutelHeader->addProcessor(type());
  _nRun++;

  // Decode and print out Run Header information - just a check
  _runNr = runHeader->getRunNumber();
}

void EUTelAPIXTbTrackTuple::processEvent(LCEvent *event) {

  _nEvt++;
  _evtNr = event->getEventNumber();
  EUTelEventImpl *euEvent = static_cast<EUTelEventImpl *>(event);

  if (euEvent->getEventType() == kEORE) {
    streamlog_out(DEBUG5) << "EORE found: nothing else to do." << std::endl;
    return;
  }

  // Clear all event info containers
  clear();

  // try to read in hits (e.g. fitted hits in local frame)
  if (!readHits(_inputTrackerHitColName, event)) {
    return;
  }

  // read in raw data
  if (!readZsHits(_dutZsColName, event)) {
    return;
  }

  // read in tracks
  if (!readTracks(event)) {
    return;
  }

  // fill the trees
  _zstree->Fill();
  _eutracks->Fill();
  _euhits->Fill();

  _isFirstEvent = false;
}

void EUTelAPIXTbTrackTuple::end() {
  // write version number
  _versionNo->push_back(1.3);
  _versionTree->Fill();
  // Maybe some stats output?
  _file->Write();
}

// Read in TrackerHit(Impl) to later dump them
bool EUTelAPIXTbTrackTuple::readHits(std::string hitColName, LCEvent *event) {

  LCCollection *hitCollection = nullptr;

  try {
    hitCollection = event->getCollection(hitColName);
  } catch (lcio::DataNotAvailableException &e) {
    streamlog_out(DEBUG2) << "Hit collection " << hitColName
                          << " not found in event " << event->getEventNumber()
                          << "!" << std::endl;
    return false;
  }

  int nHit = hitCollection->getNumberOfElements();
  _nHits = nHit;

  for (int ihit = 0; ihit < nHit; ihit++) {
    TrackerHitImpl *meshit =
        dynamic_cast<TrackerHitImpl *>(hitCollection->getElementAt(ihit));
    const double *pos = meshit->getPosition();
    LCObjectVec clusterVec = (meshit->getRawHits());

    UTIL::CellIDDecoder<TrackerHitImpl> hitDecoder(EUTELESCOPE::HITENCODING);
    int sensorID = hitDecoder(meshit)["sensorID"];

    // Only dump DUT hits
    if (std::find(_DUTIDs.begin(), _DUTIDs.end(), sensorID) == _DUTIDs.end()) {
      continue;
    }

    double x = pos[0];
    double y = pos[1];
    double z = pos[2];

    // offset by half sensor/sensitive size
    _hitXPos->push_back(x + _xSensSize.at(sensorID) / 2.0);
    _hitYPos->push_back(y + _ySensSize.at(sensorID) / 2.0);
    _hitZPos->push_back(z);
    _hitSensorId->push_back(sensorID);
  }

  return true;
}

// Read in TrackerHit to later dump
bool EUTelAPIXTbTrackTuple::readTracks(LCEvent *event) {

  LCCollection *trackCol = nullptr;

  try {
    trackCol = event->getCollection(_inputTrackColName);
  } catch (lcio::DataNotAvailableException &e) {
    streamlog_out(DEBUG2) << "Track collection " << _inputTrackColName
                          << " not found in event " << event->getEventNumber()
                          << "!" << std::endl;
    return false;
  }

  // setup cellIdDecoder to decode the hit properties
  UTIL::CellIDDecoder<TrackerHitImpl> hitCellDecoder(EUTELESCOPE::HITENCODING);

  int nTrackParams = 0;
  for (int itrack = 0; itrack < trackCol->getNumberOfElements(); itrack++) {
    lcio::Track *fittrack =
        dynamic_cast<lcio::Track *>(trackCol->getElementAt(itrack));

    std::vector<EVENT::TrackerHit *> trackhits = fittrack->getTrackerHits();
    double chi2 = fittrack->getChi2();
    double ndof = fittrack->getNdf();
    double dxdz = fittrack->getOmega();
    double dydz = fittrack->getPhi();

    /* Get the (fitted) hits belonging to this track,
       they are in global frame when coming from the straight track fitter */
    for (unsigned int ihit = 0; ihit < trackhits.size(); ihit++) {
      TrackerHitImpl *fittedHit =
          dynamic_cast<TrackerHitImpl *>(trackhits.at(ihit));
      const double *pos = fittedHit->getPosition();
      if ((hitCellDecoder(fittedHit)["properties"] & kFittedHit) == 0) {
        continue;
      }

      UTIL::CellIDDecoder<TrackerHitImpl> hitDecoder(EUTELESCOPE::HITENCODING);
      int sensorID = hitDecoder(fittedHit)["sensorID"];

      // Dump the (fitted) hits for the DUTs
      if (std::find(_DUTIDs.begin(), _DUTIDs.end(), sensorID) ==
          _DUTIDs.end()) {
        continue;
      }

      nTrackParams++;
      /* Transform to local coordinates */
      double pos_loc[3];
      geo::gGeometry().master2Local(sensorID, pos, pos_loc);

      double x = pos_loc[0];
      double y = pos_loc[1];

      // eutrack tree
      _xPos->push_back(x);
      _yPos->push_back(y);
      _dxdz->push_back(dxdz);
      _dydz->push_back(dydz);
      _trackIden->push_back(sensorID);
      _trackNum->push_back(itrack);
      _chi2->push_back(chi2);
      _ndof->push_back(ndof);
    }
  }

  _nTrackParams = nTrackParams;
  return true;
}

// Read in raw (zs) TrackerData(Impl) to later dump
bool EUTelAPIXTbTrackTuple::readZsHits(std::string colName, LCEvent *event) {

  LCCollectionVec *zsInputCollectionVec = nullptr;

  try {
    zsInputCollectionVec =
        dynamic_cast<LCCollectionVec *>(event->getCollection(colName));
  } catch (DataNotAvailableException &e) {
    streamlog_out(DEBUG2) << "Raw ZS data collection " << colName
                          << " not found in event " << event->getEventNumber()
                          << "!" << std::endl;
    return false;
  }

  UTIL::CellIDDecoder<TrackerDataImpl> cellDecoder(zsInputCollectionVec);
  for (unsigned int plane = 0; plane < zsInputCollectionVec->size(); plane++) {
    TrackerDataImpl *zsData = dynamic_cast<TrackerDataImpl *>(
        zsInputCollectionVec->getElementAt(plane));
    SparsePixelType type = static_cast<SparsePixelType>(
        static_cast<int>(cellDecoder(zsData)["sparsePixelType"]));
    int sensorID = cellDecoder(zsData)["sensorID"];

    if (type == kEUTelGenericSparsePixel) {
      auto sparseData = std::make_unique<
          EUTelTrackerDataInterfacerImpl<EUTelGenericSparsePixel>>(zsData);

      for (auto &apixPixel : *sparseData) {
        _nPixHits++;
        p_iden->push_back(sensorID);
        p_row->push_back(apixPixel.getYCoord());
        p_col->push_back(apixPixel.getXCoord());
        p_tot->push_back(static_cast<int>(apixPixel.getSignal()));
        p_lv1->push_back(static_cast<int>(apixPixel.getTime()));
      }

    } else if (type == kEUTelMuPixel) {
      auto sparseData =
          std::make_unique<EUTelTrackerDataInterfacerImpl<EUTelMuPixel>>(
              zsData);
      for (auto &binaryPixel : *sparseData) {
        _nPixHits++;
        p_iden->push_back(sensorID);
        p_row->push_back(binaryPixel.getYCoord());
        p_col->push_back(binaryPixel.getXCoord());
        p_hitTime->push_back(binaryPixel.getHitTime());
        p_frameTime->push_back(binaryPixel.getFrameTime());
      }
    } else {
      throw UnknownDataTypeException("Unknown sparsified pixel");
    }
  }
  return true;
}

void EUTelAPIXTbTrackTuple::clear() {
  /* Clear zsdata */
  p_col->clear();
  p_row->clear();
  p_tot->clear();
  p_iden->clear();
  p_lv1->clear();
  p_hitTime->clear();
  p_frameTime->clear();
  _nPixHits = 0;
  /* Clear hittrack */
  _xPos->clear();
  _yPos->clear();
  _dxdz->clear();
  _dydz->clear();
  _trackNum->clear();
  _trackIden->clear();
  _chi2->clear();
  _ndof->clear();
  // Clear hits
  _hitXPos->clear();
  _hitYPos->clear();
  _hitZPos->clear();
  _hitSensorId->clear();
}

void EUTelAPIXTbTrackTuple::prepareTree() {
  _file = new TFile(_path2file.c_str(), "RECREATE");

  _xPos = new std::vector<double>();
  _yPos = new std::vector<double>();
  _dxdz = new std::vector<double>();
  _dydz = new std::vector<double>();
  _trackIden = new std::vector<int>();
  _trackNum = new std::vector<int>();
  _chi2 = new std::vector<double>();
  _ndof = new std::vector<double>();

  p_col = new std::vector<int>();
  p_row = new std::vector<int>();
  p_tot = new std::vector<int>();
  p_iden = new std::vector<int>();
  p_lv1 = new std::vector<int>();
  p_hitTime = new std::vector<int>();
  p_frameTime = new std::vector<double>();

  _hitXPos = new std::vector<double>();
  _hitYPos = new std::vector<double>();
  _hitZPos = new std::vector<double>();
  _hitSensorId = new std::vector<int>();

  _versionNo = new std::vector<double>();
  _versionTree = new TTree("version", "version");
  _versionTree->Branch("no", &_versionNo);

  _euhits = new TTree("fitpoints", "fitpoints");
  _euhits->SetAutoSave(1000000000);

  _euhits->Branch("nHits", &_nHits);
  _euhits->Branch("xPos", &_hitXPos);
  _euhits->Branch("yPos", &_hitYPos);
  _euhits->Branch("zPos", &_hitZPos);
  _euhits->Branch("sensorId", &_hitSensorId);

  _zstree = new TTree("rawdata", "rawdata");
  _zstree->SetAutoSave(1000000000);
  _zstree->Branch("nPixHits", &_nPixHits);
  _zstree->Branch("euEvt", &_nEvt);
  _zstree->Branch("col", &p_col);
  _zstree->Branch("row", &p_row);
  _zstree->Branch("tot", &p_tot);
  _zstree->Branch("lv1", &p_lv1);
  _zstree->Branch("iden", &p_iden);
  _zstree->Branch("hitTime", &p_hitTime);
  _zstree->Branch("frameTime", &p_frameTime);

  // Tree for storing all track param info
  _eutracks = new TTree("tracks", "tracks");
  _eutracks->SetAutoSave(1000000000);
  _eutracks->Branch("nTrackParams", &_nTrackParams);
  _eutracks->Branch("euEvt", &_nEvt);
  _eutracks->Branch("xPos", &_xPos);
  _eutracks->Branch("yPos", &_yPos);
  _eutracks->Branch("dxdz", &_dxdz);
  _eutracks->Branch("dydz", &_dydz);
  _eutracks->Branch("trackNum", &_trackNum);
  _eutracks->Branch("iden", &_trackIden);
  _eutracks->Branch("chi2", &_chi2);
  _eutracks->Branch("ndof", &_ndof);

  _euhits->AddFriend(_zstree);
  _euhits->AddFriend(_eutracks);
}
