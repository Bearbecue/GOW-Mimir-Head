//----------------------------------------------------------------------------

#include <Arduino.h>
#include "Helpers.h"

//----------------------------------------------------------------------------

void  LoopTimer::tick()
{
  const long int  curMS = millis();
  if (m_PrevMS < 0)
    m_PrevMS = curMS;

  m_DtMS = (curMS > m_PrevMS) ? curMS - m_PrevMS : 10;  // by default 10 ms when wrapping around
  m_PrevMS = curMS;

  m_ElapsedTime += m_DtMS * 1.0e-3f;
}

//----------------------------------------------------------------------------
