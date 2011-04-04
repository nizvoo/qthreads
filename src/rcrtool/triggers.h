#ifndef QTHREAD_RCRTOOL_TRIGGERS_H
#define QTHREAD_RCRTOOL_TRIGGERS_H

/*! 
 * 
 */
typedef enum _rcrtool_trigger_type {
    TYPE_CORE = 0,
    TYPE_SOCKET,
    TYPE_NODE
} rcrtool_trigger_type;

/*!
 * Data structure holding the trigger information.  Includes info about 
 * thresholds for each kind of metric and the key for the shared mem location.
 */
typedef struct trigger {
	int    type;           // TYPE_CORE, TYPE_SOCKET or TYPE_NODE
	int    id;             // core/socket/node id
	char*  meterName;      // metric name, for eg. "MemoryConcurrency"
	int    meterNum;       // index into array of meter names
	key_t  flagShmKey;     // the shared memory key for the trigger flag
	key_t  appStateShmKey; // the shared memory key for the application state
	double threshold_ub;   // upper bound of metric value
	double threshold_lb;   // lower bound of metric value
} Trigger;

/***********************************************************************
* definition for the trigger map; used by all methods
***********************************************************************/
extern Trigger** triggerMap;   // allocated and pupulated in buildTriggerMap()
extern int       numTriggers;  // populated in buildTriggerMap()

#endif
