export interface Invitation { code: string; passcode: string }
export function readInvitation(location: Pick<Location, 'hash' | 'search'>): Invitation {
  const data = new URLSearchParams(location.hash.slice(1));
  const code = data.get('code'), passcode = data.get('passcode');
  if (data.getAll('code').length !== 1 || data.getAll('passcode').length !== 1 ||
      !code || !passcode || data.has('backend') || new URLSearchParams(location.search).has('backend'))
    throw new Error('invalid_invitation');
  return { code, passcode };
}
