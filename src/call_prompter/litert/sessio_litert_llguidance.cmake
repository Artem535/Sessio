# LiteRT-LM v0.18.0's tokenizer callback accepts text, not arbitrary bytes.
# llguidance can force an incomplete UTF-8 prefix when enum values share only
# the first byte (e.g. Анна/Борис). Its string-tokenizer mode prevents passing
# that prefix to TextToTokenIds(), where it becomes the replacement character.
# Generate a local compatibility source; leave the upstream checkout intact.
function(sessio_litert_prepare_llguidance source output)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(READ "${source}" _provider)
  set(_tokens "std::vector<std::string> tokens = tokenizer.GetTokens();")
  if(NOT _provider MATCHES "std::vector<std::string> tokens = tokenizer.GetTokens\\(\\);")
    message(FATAL_ERROR "[Sessio] Unsupported llguidance vocabulary adapter: ${source}")
  endif()
  string(REPLACE "${_tokens}"
    "${_tokens}\n  for (auto &piece : tokens) piece = sessio::litert::detail::sentencePieceTokenBytes(std::move(piece));"
    _provider "${_provider}")
  string(PREPEND _provider "#include \"token_bytes.h\"\n")
  if(_provider MATCHES "\\.tokenize_assumes_string = false")
    string(REPLACE ".tokenize_assumes_string = false" ".tokenize_assumes_string = true"
      _provider "${_provider}")
  elseif(NOT _provider MATCHES "\\.tokenize_assumes_string = true")
    message(FATAL_ERROR "[Sessio] Unsupported llguidance tokenizer adapter: ${source}")
  endif()
  file(WRITE "${output}" "${_provider}")
endfunction()
