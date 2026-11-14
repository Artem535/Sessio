import { type Invitation } from './invitation';
import { validateDisplayName } from './display-name';
export interface JoinToken { endpointUrl: string; roomName: string; token: string; expiresAt: number }
export async function joinGuest(invitation: Invitation, name: string, signal: AbortSignal): Promise<JoinToken> {
  const response = await fetch(`/v1/invitations/${encodeURIComponent(invitation.code)}/client-token`, {
    method:'POST', headers:{'Content-Type':'application/json'}, cache:'no-store', credentials:'omit', signal,
    body:JSON.stringify({passcode:invitation.passcode,displayName:validateDisplayName(name),clientKind:'web'})
  });
  if (!response.ok) throw new Error(response.status === 429 ? 'too_many_attempts' :
      response.status === 410 ? 'meeting_window_closed' : 'join_denied');
  let value: JoinToken;
  try { value = await response.json(); } catch { throw new Error('invalid_response'); }
  if (typeof value?.endpointUrl !== 'string' || !value.endpointUrl.startsWith('wss://') ||
      typeof value.roomName !== 'string' || !value.roomName || typeof value.token !== 'string' ||
      !value.token || typeof value.expiresAt !== 'number' || !Number.isFinite(value.expiresAt))
    throw new Error('invalid_response');
  return value;
}
