//----------------------------------------------------------------------------
//  Various helper functions

template<typename _T, typename _TA, typename _TB>
static inline _T clamp(_T v, _TA vMin, _TB vMax) { return v < vMin ? vMin : v > vMax ? vMax : v; }

template<typename _T>
static inline _T sign(_T v) { return v < 0 ? -1 : 1; }

//----------------------------------------------------------------------------

#define STR_STARTS_WITH(__str, __literal) (!strncmp(__str, __literal, sizeof(__literal)-1))

//----------------------------------------------------------------------------

class LoopTimer
{
public:
  void      tick();

  int       dtMS() const { return m_DtMS; }
  float     dt() const { return m_DtMS * 1.0e-3f; }
  float     elapsedTime() const { return (float)m_ElapsedTime; }

private:
  int       m_DtMS = 0;
  long int  m_PrevMS = -1;
  double    m_ElapsedTime = 0;
};

//----------------------------------------------------------------------------

extern float  Noise(float t);
static float  smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

struct CRGB;
extern CRGB   ShiftHS(const CRGB &color, int32_t hue_shift, int32_t sat_shift);

//----------------------------------------------------------------------------
