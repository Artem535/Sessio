import { expect, test } from 'vitest';
import { validateDisplayName } from '../src/display-name';
import cases from '../../../testdata/display-name-cases.json';
test('matches shared backend vectors',()=>{
  for(const item of cases) {
    if(item.accepted) expect(validateDisplayName(item.value)).toBe(item.normalised);
    else expect(()=>validateDisplayName(item.value)).toThrow('invalid_display_name');
  }
});
test('trims, counts scalars, rejects controls and unpaired surrogates', () => {
  expect(validateDisplayName(' Гость　')).toBe('Гость');
  expect(validateDisplayName('😀'.repeat(80))).toHaveLength(160);
  for (const name of ['', '  ', '😀'.repeat(81), 'a\u0085', '\ud800'])
    expect(() => validateDisplayName(name)).toThrow('invalid_display_name');
});
