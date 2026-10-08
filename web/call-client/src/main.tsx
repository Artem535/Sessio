import { createRoot } from 'react-dom/client';
import { App } from './App';
import { readInvitation, type Invitation } from './invitation';
import './styles.css';
let invitation:Invitation|null=null;
try{invitation=readInvitation(location);}catch{/* safe invalid link screen */}
history.replaceState(null,'',location.pathname);
createRoot(document.getElementById('root')!).render(<App invitation={invitation}/>);
