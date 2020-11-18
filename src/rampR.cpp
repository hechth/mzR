#include<Rcpp.h>

#ifdef __MINGW32__
#undef Realloc
#undef Free
//#include <winsock2.h>
#endif


#include <stdio.h>

#include <R.h>
#include <Rinternals.h>

#include "ramp.h"

#define MAX_RAMP_FILES 100
#define NEW_LIST(n)		Rf_allocVector(VECSXP,n)
#define NEW_CHARACTER(n)	Rf_allocVector(STRSXP,n)
#define SET_NAMES(x, n)		Rf_setAttrib(x, R_NamesSymbol, n)
#define SET_CLASS(x, n)		Rf_setAttrib(x, R_ClassSymbol, n)
#define INTEGER_POINTER(x)	INTEGER(x)
#define NEW_INTEGER(n)		Rf_allocVector(INTSXP,n)
#define NEW_NUMERIC(n)		Rf_allocVector(REALSXP,n)
#define NUMERIC_POINTER(x)	REAL(x)
#define SET_LEVELS(x, l)       	Rf_setAttrib(x, R_LevelsSymbol, l)
extern"C" {

    typedef struct
    {
        RAMPFILE          *file;
        ramp_fileoffset_t *index;
        int               numscans;
    } RAMPSTRUCT;

    static int        rampInitalized = 0;
    static RAMPSTRUCT rampStructs[MAX_RAMP_FILES];

    void RampRInit(void)
    {

        int    i;

        for (i = 0; i < MAX_RAMP_FILES; i++)
        {
            rampStructs[i].file = NULL;
            rampStructs[i].index = NULL;
            rampStructs[i].numscans = 0;
        }

        rampInitalized = 1;
    }

    void RampRPrintFiles(void)
    {

        int i;

        if (!rampInitalized)
            RampRInit();

        for (i = 0; i < MAX_RAMP_FILES; i++)
            if (rampStructs[i].file)
                Rprintf("File %i (%i scans)\n", i, rampStructs[i].numscans);
    }

    int RampRFreeHandle(void)
    {

        int i;

        if (!rampInitalized)
            RampRInit();

        for (i = 0; i < MAX_RAMP_FILES; i++)
            if (!rampStructs[i].file)
                return i;

        return -1;
    }

    void RampRIsFile(const char *fileName[], int *isfile)
    {

        RAMPFILE *rampfile;

        *isfile = 0;

        rampfile = rampOpenFile(fileName[0]);
        if (!rampfile)
            return;

        rampCloseFile(rampfile);

        *isfile = 1;
    }

    void RampROpen(const char *fileName[], int *rampid, int *status)
    {

        ramp_fileoffset_t indexOffset;
        int               numscans;

        if (!rampInitalized)
            RampRInit();

        *status = -1;

        *rampid = RampRFreeHandle();
        if (*rampid < 0)
        {
            *status = *rampid;
            return;
        }

        rampStructs[*rampid].file = rampOpenFile(fileName[0]);
        if (!rampStructs[*rampid].file)
            return;

        indexOffset = getIndexOffset(rampStructs[*rampid].file);

        rampStructs[*rampid].index = readIndex(rampStructs[*rampid].file,
                                               indexOffset, &numscans);

        if (!rampStructs[*rampid].index || numscans < 1)
        {
            rampStructs[*rampid].file = NULL;
            if (rampStructs[*rampid].index)
                free(rampStructs[*rampid].index);
            rampStructs[*rampid].index = NULL;
            return;
        }

        rampStructs[*rampid].numscans = numscans;
        *status = 0;
    }

    void RampRClose(const int *rampid)
    {

        if (!rampInitalized)
            return;

        if (*rampid < 0 || *rampid >= MAX_RAMP_FILES)
            return;

        if (rampStructs[*rampid].file)
            rampCloseFile(rampStructs[*rampid].file);
        rampStructs[*rampid].file = NULL;

        if (rampStructs[*rampid].index)
            free(rampStructs[*rampid].index);
        rampStructs[*rampid].index = NULL;

        rampStructs[*rampid].numscans = 0;
    }

    void RampRCloseAll(void)
    {

        int i;

        if (!rampInitalized)
            return;

        for (i = 0; i < MAX_RAMP_FILES; i++)
            if (rampStructs[i].file)
                RampRClose(&i);
    }

    void RampRNumScans(const int *rampid, int *numscans, int *status)
    {

        if (!rampInitalized)
            RampRInit();

        *status = -1;

        if (*rampid < 0 || *rampid >= MAX_RAMP_FILES)
            return;

        *numscans = rampStructs[*rampid].numscans;

        if (*numscans)
            *status = 0;
    }

    SEXP RampRScanHeaders(SEXP rampid)
    {

        int               i, j, id, numscans, ncol = 18, ntypes = 0, stlen = 10;
        SEXP              result = PROTECT(NEW_LIST(ncol));
        SEXP              temp;
        SEXP              names;
        struct            ScanHeaderStruct scanHeader;
        RAMPFILE          *file;
        ramp_fileoffset_t *index;
        char              rowname[20], *scanTypes;
        int               *seqNum, *acquisitionNum, *msLevel, *peaksCount,
                          *precursorScanNum, *precursorCharge, *scanType, *polarity;
        double            *totIonCurrent, *retentionTime, *basePeakMZ,
                          *basePeakIntensity, *collisionEnergy, *ionisationEnergy,
                          *lowMZ, *highMZ, *precursorMZ, *precursorIntensity;

        if (!rampInitalized)
            RampRInit();

        if (Rf_length(rampid) != 1)
            Rf_error("rampid must be of length 1");

        id = *INTEGER_POINTER(rampid);
        if (id < 0 || id >= MAX_RAMP_FILES || !rampStructs[id].file)
            Rf_error("invalid rampid");

        file = rampStructs[id].file;
        index = rampStructs[id].index;
        numscans = rampStructs[id].numscans;

        SET_NAMES(result, names = NEW_CHARACTER(ncol));

        Rf_setAttrib(result, Rf_install("row.names"), temp = NEW_CHARACTER(numscans));
        for (i = 0; i < numscans; i++)
        {
            sprintf(rowname, "%i", i+1);
            SET_STRING_ELT(temp, i, Rf_mkChar(rowname));
        }

        SET_CLASS(result, temp = NEW_CHARACTER(1));
        SET_STRING_ELT(temp, 0, Rf_mkChar("data.frame"));

        SET_VECTOR_ELT(result, 0, temp = NEW_INTEGER(numscans));
        seqNum = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 0, Rf_mkChar("seqNum"));

        SET_VECTOR_ELT(result, 1, temp = NEW_INTEGER(numscans));
        acquisitionNum = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 1, Rf_mkChar("acquisitionNum"));

        SET_VECTOR_ELT(result, 2, temp = NEW_INTEGER(numscans));
        msLevel = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 2, Rf_mkChar("msLevel"));

        SET_VECTOR_ELT(result, 3, temp = NEW_INTEGER(numscans));
        peaksCount = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 3, Rf_mkChar("peaksCount"));

        SET_VECTOR_ELT(result, 4, temp = NEW_NUMERIC(numscans));
        totIonCurrent = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 4, Rf_mkChar("totIonCurrent"));

        SET_VECTOR_ELT(result, 5, temp = NEW_NUMERIC(numscans));
        retentionTime = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 5, Rf_mkChar("retentionTime"));

        SET_VECTOR_ELT(result, 6, temp = NEW_NUMERIC(numscans));
        basePeakMZ = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 6, Rf_mkChar("basePeakMZ"));

        SET_VECTOR_ELT(result, 7, temp = NEW_NUMERIC(numscans));
        basePeakIntensity = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 7, Rf_mkChar("basePeakIntensity"));

        SET_VECTOR_ELT(result, 8, temp = NEW_NUMERIC(numscans));
        collisionEnergy = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 8, Rf_mkChar("collisionEnergy"));

        SET_VECTOR_ELT(result, 9, temp = NEW_NUMERIC(numscans));
        ionisationEnergy = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 9, Rf_mkChar("ionisationEnergy"));

        SET_VECTOR_ELT(result, 10, temp = NEW_NUMERIC(numscans));
        lowMZ = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 10, Rf_mkChar("lowMZ"));

        SET_VECTOR_ELT(result, 11, temp = NEW_NUMERIC(numscans));
        highMZ = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 11, Rf_mkChar("highMZ"));

        SET_VECTOR_ELT(result, 12, temp = NEW_INTEGER(numscans));
        precursorScanNum = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 12, Rf_mkChar("precursorScanNum"));

        SET_VECTOR_ELT(result, 13, temp = NEW_NUMERIC(numscans));
        precursorMZ = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 13, Rf_mkChar("precursorMZ"));

        SET_VECTOR_ELT(result, 14, temp = NEW_INTEGER(numscans));
        precursorCharge = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 14, Rf_mkChar("precursorCharge"));

        SET_VECTOR_ELT(result, 15, temp = NEW_INTEGER(numscans));
        scanType = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 15, Rf_mkChar("scanType"));

        SET_VECTOR_ELT(result, 16, temp = NEW_NUMERIC(numscans));
        precursorIntensity = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 16, Rf_mkChar("precursorIntensity"));

        SET_VECTOR_ELT(result, 17, temp = NEW_INTEGER(numscans));
        polarity = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 17, Rf_mkChar("polarity"));

        scanTypes = S_alloc(stlen*SCANTYPE_LENGTH, sizeof(char));

        for (i = 0; i < numscans; i++)
        {
            readHeader(file, index[i+1], &scanHeader);
            seqNum[i] = scanHeader.seqNum;
            acquisitionNum[i] = scanHeader.acquisitionNum;
            msLevel[i] = scanHeader.msLevel;
            peaksCount[i] = scanHeader.peaksCount;
            totIonCurrent[i] = scanHeader.totIonCurrent;
            retentionTime[i] = scanHeader.retentionTime;
            basePeakMZ[i] = scanHeader.basePeakMZ;
            basePeakIntensity[i] = scanHeader.basePeakIntensity;
            collisionEnergy[i] = scanHeader.collisionEnergy;
            ionisationEnergy[i] = scanHeader.ionisationEnergy;
            lowMZ[i] = scanHeader.lowMZ;
            highMZ[i] = scanHeader.highMZ;
            precursorScanNum[i] = scanHeader.precursorScanNum;
            precursorMZ[i] = scanHeader.precursorMZ;
            precursorIntensity[i] = scanHeader.precursorIntensity;
            polarity[i] = ! scanHeader.is_negative;
            precursorCharge[i] = scanHeader.precursorCharge;
            for (j = 0; j < ntypes; j++)
                if (!strcmp(scanHeader.scanType, scanTypes+j*SCANTYPE_LENGTH))
                {
                    scanType[i] = j+1;
                    break;
                }
            if (j == ntypes)
            {
                if (j >= stlen)
                {
                    stlen *= 2;
                    scanTypes = S_realloc(scanTypes, stlen*SCANTYPE_LENGTH/2,
                                          stlen*SCANTYPE_LENGTH, sizeof(char));
                }
                strcpy(scanTypes+j*SCANTYPE_LENGTH, scanHeader.scanType);
                ntypes++;
                scanType[i] = j+1;
            }
        }

        SET_LEVELS(VECTOR_ELT(result, 16), temp = NEW_CHARACTER(ntypes));
        for (i = 0; i < ntypes; i++)
            SET_STRING_ELT(temp, i, Rf_mkChar(scanTypes+i*SCANTYPE_LENGTH));

        SET_CLASS(VECTOR_ELT(result, 15), temp = NEW_CHARACTER(1));
        SET_STRING_ELT(temp, 0, Rf_mkChar("factor"));

        UNPROTECT(1);

        return result;
    }

    SEXP RampRSIPeaks(SEXP rampid, SEXP seqNum, SEXP peaksCount)
    {

        SEXP              result = PROTECT(NEW_LIST(3)), temp, names;
        RAMPFILE          *file;
        ramp_fileoffset_t *index;
        int               i, j, id, numscans, numpeaks, *seqNumPtr, *peaksCountPtr,
                          *scanindex;
        double            *mz, *intensity;
        RAMPREAL          *peaks, *peaksPtr;

        if (!rampInitalized)
            RampRInit();

        if (Rf_length(rampid) != 1)
            Rf_error("rampid must be of length 1");

        if (Rf_length(seqNum) != Rf_length(peaksCount))
            Rf_error("seqNum and peaksCount must be the same length");

        id = *INTEGER_POINTER(rampid);
        if (id < 0 || id >= MAX_RAMP_FILES || !rampStructs[id].file)
            Rf_error("invalid rampid");

        file = rampStructs[id].file;
        index = rampStructs[id].index;

        seqNumPtr = INTEGER_POINTER(seqNum);
        peaksCountPtr = INTEGER_POINTER(peaksCount);
        numscans = Rf_length(seqNum);

        SET_NAMES(result, names = NEW_CHARACTER(3));

        SET_VECTOR_ELT(result, 0, temp = NEW_INTEGER(numscans));
        scanindex = INTEGER_POINTER(temp);
        SET_STRING_ELT(names, 0, Rf_mkChar("scanindex"));

        numpeaks = 0;
        for (i = 0; i < numscans; i++)
        {
            if (seqNumPtr[i] > rampStructs[id].numscans)
                Rf_error("invalid number in seqnum");
            scanindex[i] = numpeaks;
            numpeaks += peaksCountPtr[i];
        }

        SET_VECTOR_ELT(result, 1, temp = NEW_NUMERIC(numpeaks));
        mz = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 1, Rf_mkChar("mz"));

        SET_VECTOR_ELT(result, 2, temp = NEW_NUMERIC(numpeaks));
        intensity = NUMERIC_POINTER(temp);
        SET_STRING_ELT(names, 2, Rf_mkChar("intensity"));

        for (i = 0; i < numscans; i++)
        {
            if (peaksCountPtr[i] != readPeaksCount(file, index[seqNumPtr[i]]))
                Rf_error("invalid number in peaksCount");
            if (peaksCountPtr[i] == 0)
                continue;
            peaks = readPeaks(file, index[seqNumPtr[i]]);
            if (!peaks)
                Rf_error("unknown problem while reading peaks");
            peaksPtr = peaks;
            for (j = 0; j < peaksCountPtr[i]; j++)
            {
                if (*peaksPtr < 0)
                    Rf_error("unexpected end of peak list");
                mz[j+scanindex[i]] = *(peaksPtr++);
                intensity[j+scanindex[i]] = *(peaksPtr++);
            }
            free(peaks);
        }

        UNPROTECT(1);

        return result;
    }

}
