export function validateDisplayName(value: string): string {
  const name = value.trim();
  if ([...name].length < 1 || [...name].length > 80 ||
      /[\u0000-\u001f\u007f-\u009f\ud800-\udfff]/u.test(name))
    throw new Error('invalid_display_name');
  return name;
}
