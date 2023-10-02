//----------------------------------------------------------------------------
//  Various helper functions

template<typename _T, typename _TA, typename _TB>
static inline _T clamp(_T v, _TA vMin, _TB vMax) { return v < vMin ? vMin : v > vMax ? vMax : v; }

//----------------------------------------------------------------------------

#define STR_STARTS_WITH(__str, __literal) (!strncmp(__str, __literal, sizeof(__literal)-1))

//----------------------------------------------------------------------------

class LoopTimer
{
public:
  void      tick();

  int       dtMS() const { return m_DtMS; }
  float     dt() const { return m_DtMS * 1.0e-3f; }
  float     elapsedTime() const { return m_ElapsedTime; }

private:
  int       m_DtMS = 0;
  long int  m_PrevMS = -1;
  float     m_ElapsedTime = 0;
};

//----------------------------------------------------------------------------
