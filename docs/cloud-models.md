# Cloud models

The meeting summary, the key points during a call and the agreement prompter can run on a cloud model instead of the local Gemma model. The cloud is optional and off by default.

## Setting up

**Settings → Cloud models**

1. Enter the provider's HTTPS address (for example `https://api.example.com/v1`; Sessio appends `/chat/completions`), the model name and your API key. Local loopback providers may use HTTP. The key is stored in the system keychain, never in `Config.yaml`; wait for the saving status to finish. A failed keychain operation is shown in the page.
2. Press **Test connection**. Sessio asks the provider for a reply in strict JSON-schema mode. A provider that does not support `response_format: json_schema` is not accepted, and there is no weaker fallback.
3. Press **Enable cloud models…**, read the terms and accept them. Consent belongs to that address: changing the address asks again, and **Withdraw consent** switches every task back to the local model.
4. Choose per task (agreements, key points, meeting summary) whether it runs locally or in the cloud.

Anything with an OpenAI-compatible API works: hosted providers and your own servers (vLLM, Ollama, LM Studio). GigaChat is not compatible directly; use an OpenAI-compatible proxy in front of it.

## What is sent

Phrases of the call transcript, the agreements found so far and the notes used to build key points and the summary. Audio and video are never sent.

By default names, phone numbers and e-mail addresses of the client (from their card) and of you are replaced by labels (`[Клиент]`, `[Специалист]`, `[Телефон 1]`, `[Email 1]`) before sending, and put back in the reply. Phone numbers and e-mail addresses found in the text are replaced too. Other details said aloud, such as third parties and places, are **not** hidden. The option can be turned off in the same page.

## When the cloud is unavailable

The call and its transcription go on. Sessio shows "Cloud unavailable: …" with the reason (key rejected, model not found, rate limit, no connection, timeout) and does not switch to the local model on its own. Live key points and the agreements prompter retry on temporary failures (no connection, rate limit, server error) and stop on ones you must fix (key, model, provider).

Each summary and the key points show which engine wrote them ("Prepared by AI · Cloud · model").

## Speed

Cloud summaries read several parts of the call at once (up to four requests in parallel). Completion time depends on the provider, network and conversation length.

## Limits

* The context-size field is stored but not used yet: parts of the call keep the size chosen for the local model.
* Names that are not on a client card or in your display name are not hidden.
