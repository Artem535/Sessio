import { expect, test } from 'vitest';
import { readInvitation } from '../src/invitation';
test('accepts only unique fragment credentials and no backend override', () => {
  expect(readInvitation({hash:'#code=a&passcode=b',search:''})).toEqual({code:'a',passcode:'b'});
  for (const hash of ['', '#code=a', '#code=a&code=b&passcode=c', '#code=a&passcode=b&backend=x'])
    expect(() => readInvitation({hash,search:''})).toThrow('invalid_invitation');
  expect(() => readInvitation({hash:'#code=a&passcode=b',search:'?backend=x'})).toThrow('invalid_invitation');
});
