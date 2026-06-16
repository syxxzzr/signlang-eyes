#include "speech_asr_result.hpp"

namespace signlang::speech_asr {

  auto language_code(AsrLanguage language) -> const char* {
    switch (language) {
    case AsrLanguage::English:
      return "en";
    case AsrLanguage::Chinese:
      return "zh";
    }

    return "en";
  }

} // namespace signlang::speech_asr
