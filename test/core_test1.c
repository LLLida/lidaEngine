/*
  This tests core engine utilities. This maybe the most important test.
 */
#include "base.h"

int
main(int argc, char** argv)
{
  /*
    1. Test logging.
   */
  lida_InitPlatformSpecificLoggers();
  LIDA_LOG_TRACE("This is a TRACE message");
  LIDA_LOG_DEBUG("This is a DEBUG message");
  LIDA_LOG_INFO("This is a INFO message");
  LIDA_LOG_WARN("This is a WARN message");
  LIDA_LOG_ERROR("This is a ERROR message");
  LIDA_LOG_FATAL("This is a FATAL message");

  /*
    2. Test hash tables.
   */
  return 0;
}
