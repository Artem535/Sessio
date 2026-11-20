import { useEffect, useRef, useState } from 'react';
import { AppIcon } from './Icons';
import { text } from './copy';

// A custom-scheme link fails silently when no app handles it. If the page
// keeps focus for a moment after the click, nothing took over: say so.
export function NativeLink({href,compact}:{href:string;compact?:boolean}) {
  const [hint,setHint] = useState(false), timer = useRef(0);
  useEffect(()=>()=>clearTimeout(timer.current),[]);
  const open=()=>{setHint(false);clearTimeout(timer.current);let left=false;const away=()=>{left=true;};
    window.addEventListener('blur',away,{once:true});
    timer.current=window.setTimeout(()=>{window.removeEventListener('blur',away);if(!left && !document.hidden)setHint(true);},1500);};
  return <div className={'native'+(compact?' compact':'')}><a className="button secondary" href={href} rel="noreferrer" onClick={open} title={text.open}><AppIcon/><span>{text.open}</span></a>
    {hint && <p className="hint" role="status">{text.appHint}</p>}</div>;
}
