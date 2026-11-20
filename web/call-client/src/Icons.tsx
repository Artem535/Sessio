// Same glyphs as the desktop call controls (src/widgets/call_control_icons.cpp),
// drawn on the same 24px grid so both clients read alike.
const Slash = () => <line x1="4" y1="4" x2="20" y2="20" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/>;

export function MicIcon({off}:{off?:boolean}) {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="9" y="3" width="6" height="12" rx="3" fill="currentColor"/>
    <path d="M6 15a6 6 0 0 0 12 0M12 18v3M8 21h8" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/>{off && <Slash/>}</svg>;
}

export function CameraIcon({off}:{off?:boolean}) {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="2" y="7" width="14" height="10" rx="2" fill="currentColor"/>
    <path d="M16 10l6-4v12l-6-4z" fill="currentColor"/>{off && <Slash/>}</svg>;
}

export function ScreenIcon() {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="4" width="18" height="12" rx="1.5" fill="none" stroke="currentColor" strokeWidth="1.8"/>
    <path d="M9 20h6M12 16v4M12 13V7.5M9.5 10L12 7.5 14.5 10" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round"/></svg>;
}

export function DevicesIcon() {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 6h18M3 12h18M3 18h18" stroke="currentColor" strokeWidth="2" strokeLinecap="round"/>
    <circle cx="8" cy="6" r="2.5" fill="currentColor"/><circle cx="16" cy="12" r="2.5" fill="currentColor"/><circle cx="10" cy="18" r="2.5" fill="currentColor"/></svg>;
}

export function SpeakerIcon() {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 9h4l5-4v14l-5-4H4z" fill="currentColor"/>
    <path d="M16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round"/></svg>;
}

export function AppIcon() {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="4" width="18" height="14" rx="2" fill="none" stroke="currentColor" strokeWidth="1.8"/>
    <path d="M3 8h18M8 21h8" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round"/></svg>;
}
