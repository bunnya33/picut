#pragma once
#include "annotation.h"
#include "recorder.h"

// Build immutable artwork on the GUI thread. The recording worker only
// composites cached pixels and evaluates effects against each live frame.
RecordingPainter recordingPainter(const Annotation &annotation, QSize frameSize = {});
