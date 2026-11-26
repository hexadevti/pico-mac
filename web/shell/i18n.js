// pico-mac -- page translations. The language follows the browser (navigator.languages) unless the
// user picked one in the Files dialog (localStorage 'picomac-lang') or the URL has ?lang=xx.
// Static labels in index.html carry data-i18n (text), data-i18n-html, data-i18n-title, data-i18n-aria.
// The emulator's own menus stay in English, as on the PicoCalc.
'use strict';

const I18N = {
  pt: {
    _name: 'Português',
    'boot.loading': 'Carregando…',
    'boot.noSAB': 'Este navegador não liberou SharedArrayBuffer (cross-origin isolation). Abra o endereço com https:// no Chrome ou Safari (não dentro do WhatsApp/Instagram) e recarregue a página.',
    'boot.copying': 'Copiando sdcard local: {0}',
    'boot.seeding': 'Baixando {0}…', 'boot.moving': 'Movendo para o novo armazenamento: {0}', 'boot.card': 'Abrindo os arquivos…', 'boot.lost': 'Não deu para mover (grande demais para este aparelho): {0}. Envie de novo.',
    'boot.fail': 'Falha ao carregar: {0}',
    'tb.menu': 'Menu (Ctrl+Esc)', 'tb.kbd': 'Teclado', 'tb.game': 'Teclado de jogos', 'tb.files': 'Arquivos', 'tb.mouse': 'Trackpad',
    'tb.sound': 'Som',
    'mb.left': 'Botão do mouse',
    'pad.hint': 'Deslize para mover · toque = clique · toque duplo e arraste = arrastar',
    'ui.ok': 'OK', 'ui.cancel': 'Cancelar', 'ui.back': 'Voltar', 'ui.close': 'Fechar',
    'files.sub': 'Ficam salvos só neste navegador', 'files.uploadSub': 'ROMs vão para roms, discos para disks', 'files.uploadHere': 'Enviar para esta pasta', 'files.uploadHereSub': 'Escolha arquivos deste aparelho', 'files.folders': 'Pastas', 'files.more': 'Mais', 'files.count': '{0} · {1}', 'files.rebootSub': 'Salva os arquivos e recarrega a página', 'recv.sub': 'Envie de um computador com um código',
    'files.title': 'Arquivos',
    'files.hint': 'Os arquivos ficam salvos só neste navegador. Traga a sua ROM do Mac (Plus: 128 KB, Mac II: 256 KB, IIci: 512 KB) para <b>picomac/roms</b> e as imagens de disco do System (.dsk, .img, .hfv…) para <b>picomac/disks</b>. Depois escolha-as na tela de uma startup (ROM, Boot disk).',
    'files.folder': 'Pasta', 'files.upload': 'Enviar arquivos', 'files.reboot': 'Reiniciar emulador', 'files.log': 'Registro', 'files.logSub': 'O que o emulador escreveu (para relatar um problema)', 'log.copy': 'Copiar', 'log.close': 'Fechar', 'log.copied': 'Copiado', 'files.lang': 'Idioma', 'files.tapgap': 'Toque duplo', 'files.tapgapSub': 'Até {0} ms entre os toques',
    'files.empty': '(vazia)', 'files.delete': 'Apagar', 'files.confirmDelete': 'Apagar {0}?', 'files.deleteFail': 'Não foi possível apagar: {0}',
    'files.uploading': 'Enviando {0}…', 'files.uploadFail': 'Falha em {0}: {1}', 'files.saved': '{0} arquivo(s) salvo(s) em {1}/.',
    'files.download': 'Baixar',
    'confirm.reboot': 'Reiniciar o emulador?',
    'recv.start': 'Receber do PC', 'recv.stop': 'Parar',
    'recv.how': 'No computador, abra o endereço abaixo (ou <b>/send</b> neste site) e digite o código. Mantenha esta tela aberta.',
    'recv.connecting': 'Conectando…', 'recv.waiting': 'Aguardando o PC…', 'recv.pc': 'PC conectado — pode enviar.',
    'recv.getting': 'Recebendo {0}… {1}%', 'recv.saved': '{0} salvo em {1}/.', 'recv.closed': 'Conexão encerrada.',
    'recv.badFile': 'arquivo incompleto',
    'send.intro': 'Enviar arquivos do PC para o pico-mac no celular. No celular, abra 📁 → <b>Receber do PC</b> e digite aqui o código que aparece.',
    'send.code': 'Código', 'send.connect': 'Conectar', 'send.folder': 'Pasta no celular', 'send.auto': 'automática (pela extensão)',
    'send.drop': 'Arraste os arquivos para cá', 'send.pick': 'Escolher arquivos…',
    'send.badCode': 'Código inválido (5 letras/números).', 'send.connecting': 'Conectando…', 'send.connected': 'Celular conectado',
    'send.noPhone': 'Celular não encontrado — confira o código e se a tela Receber está aberta.', 'send.closed': 'Conexão encerrada.',
    'send.phoneGone': 'o celular desconectou', 'send.queued': 'na fila', 'send.sending': 'enviando…',
    'send.tooBig': 'grande demais (máx. 512 MB)', 'send.failed': 'falhou',
    'files.mouse': 'Toque na tela',
    'files.mouseTouch': 'O ponteiro vai aonde o dedo toca; segure para clicar e arrastar',
    'files.mousePad': 'Como um trackpad: deslize para mover, toque para clicar',
  },
  en: {
    _name: 'English',
    'boot.loading': 'Loading…',
    'boot.noSAB': 'This browser did not enable SharedArrayBuffer (cross-origin isolation). Open the address with https:// in Chrome or Safari (not inside WhatsApp/Instagram) and reload the page.',
    'boot.copying': 'Copying local sdcard: {0}',
    'boot.seeding': 'Downloading {0}…', 'boot.moving': 'Moving to the new storage: {0}', 'boot.card': 'Opening the files…', 'boot.lost': 'Could not be moved (too big for this device): {0}. Send it again.',
    'boot.fail': 'Failed to load: {0}',
    'tb.menu': 'Menu (Ctrl+Esc)', 'tb.kbd': 'Keyboard', 'tb.game': 'Game keys', 'tb.files': 'Files', 'tb.mouse': 'Trackpad',
    'tb.sound': 'Sound',
    'mb.left': 'Mouse button',
    'pad.hint': 'Slide to move · tap = click · double-tap and drag = drag',
    'ui.ok': 'OK', 'ui.cancel': 'Cancel', 'ui.back': 'Back', 'ui.close': 'Close',
    'files.sub': 'Stored only in this browser', 'files.uploadSub': 'ROMs go to roms, disks to disks', 'files.uploadHere': 'Upload to this folder', 'files.uploadHereSub': 'Choose files on this device', 'files.folders': 'Folders', 'files.more': 'More', 'files.count': '{0} · {1}', 'files.rebootSub': 'Saves the files and reloads the page', 'recv.sub': 'Send from a computer with a code',
    'files.title': 'Files',
    'files.hint': 'Files are stored only in this browser. Bring your own Mac ROM (Plus: 128 KB, Mac II: 256 KB, IIci: 512 KB) to <b>picomac/roms</b> and System disk images (.dsk, .img, .hfv…) to <b>picomac/disks</b>. Then pick them on a startup’s screen (ROM, Boot disk).',
    'files.folder': 'Folder', 'files.upload': 'Upload files', 'files.reboot': 'Restart emulator', 'files.log': 'Log', 'files.logSub': 'What the emulator wrote (to report a problem)', 'log.copy': 'Copy', 'log.close': 'Close', 'log.copied': 'Copied', 'files.lang': 'Language', 'files.tapgap': 'Double tap', 'files.tapgapSub': 'Up to {0} ms between taps',
    'files.empty': '(empty)', 'files.delete': 'Delete', 'files.confirmDelete': 'Delete {0}?', 'files.deleteFail': 'Could not delete: {0}',
    'files.uploading': 'Uploading {0}…', 'files.uploadFail': 'Failed on {0}: {1}', 'files.saved': '{0} file(s) saved to {1}/.',
    'files.download': 'Download',
    'confirm.reboot': 'Restart the emulator?',
    'recv.start': 'Receive from a PC', 'recv.stop': 'Stop',
    'recv.how': 'On the computer, open the address below (or <b>/send</b> on this site) and type the code. Keep this screen open.',
    'recv.connecting': 'Connecting…', 'recv.waiting': 'Waiting for the PC…', 'recv.pc': 'PC connected — send away.',
    'recv.getting': 'Receiving {0}… {1}%', 'recv.saved': '{0} saved to {1}/.', 'recv.closed': 'Connection closed.',
    'recv.badFile': 'incomplete file',
    'send.intro': 'Send files from this PC to pico-mac on your phone. On the phone, open 📁 → <b>Receive from PC</b> and type the code it shows here.',
    'send.code': 'Code', 'send.connect': 'Connect', 'send.folder': 'Folder on the phone', 'send.auto': 'automatic (by extension)',
    'send.drop': 'Drop files here', 'send.pick': 'Choose files…',
    'send.badCode': 'Invalid code (5 letters/digits).', 'send.connecting': 'Connecting…', 'send.connected': 'Phone connected',
    'send.noPhone': 'Phone not found — check the code and that the Receive screen is open.', 'send.closed': 'Connection closed.',
    'send.phoneGone': 'the phone disconnected', 'send.queued': 'queued', 'send.sending': 'sending…',
    'send.tooBig': 'too large (max 512 MB)', 'send.failed': 'failed',
    'files.mouse': 'Touch on the screen',
    'files.mouseTouch': 'The pointer goes where the finger touches; hold to click and drag',
    'files.mousePad': 'Like a trackpad: slide to move, tap to click',
  },
  es: {
    _name: 'Español',
    'boot.loading': 'Cargando…',
    'boot.noSAB': 'Este navegador no habilitó SharedArrayBuffer (cross-origin isolation). Abra la dirección con https:// en Chrome o Safari (no dentro de WhatsApp/Instagram) y recargue la página.',
    'boot.copying': 'Copiando sdcard local: {0}',
    'boot.seeding': 'Descargando {0}…', 'boot.moving': 'Moviendo al nuevo almacenamiento: {0}', 'boot.card': 'Abriendo los archivos…', 'boot.lost': 'No se pudo mover (demasiado grande para este equipo): {0}. Envíelo de nuevo.',
    'boot.fail': 'Error al cargar: {0}',
    'tb.menu': 'Menú (Ctrl+Esc)', 'tb.kbd': 'Teclado', 'tb.game': 'Teclado de juegos', 'tb.files': 'Archivos', 'tb.mouse': 'Trackpad',
    'tb.sound': 'Sonido',
    'mb.left': 'Botón del ratón',
    'pad.hint': 'Desliza para mover · toque = clic · doble toque y arrastra = arrastrar',
    'ui.ok': 'OK', 'ui.cancel': 'Cancelar', 'ui.back': 'Volver', 'ui.close': 'Cerrar',
    'files.sub': 'Se guardan solo en este navegador', 'files.uploadSub': 'Las ROMs van a roms, los discos a disks', 'files.uploadHere': 'Subir a esta carpeta', 'files.uploadHereSub': 'Elija archivos de este dispositivo', 'files.folders': 'Carpetas', 'files.more': 'Más', 'files.count': '{0} · {1}', 'files.rebootSub': 'Guarda los archivos y recarga la página', 'recv.sub': 'Envíe desde una computadora con un código',
    'files.title': 'Archivos',
    'files.hint': 'Los archivos se guardan solo en este navegador. Traiga su propia ROM del Mac (Plus: 128 KB, Mac II: 256 KB, IIci: 512 KB) a <b>picomac/roms</b> y las imágenes de disco del System (.dsk, .img, .hfv…) a <b>picomac/disks</b>. Luego elíjalas en la pantalla de un startup (ROM, Boot disk).',
    'files.folder': 'Carpeta', 'files.upload': 'Subir archivos', 'files.reboot': 'Reiniciar emulador', 'files.log': 'Registro', 'files.logSub': 'Lo que escribió el emulador (para informar un problema)', 'log.copy': 'Copiar', 'log.close': 'Cerrar', 'log.copied': 'Copiado', 'files.lang': 'Idioma', 'files.tapgap': 'Doble toque', 'files.tapgapSub': 'Hasta {0} ms entre toques',
    'files.empty': '(vacía)', 'files.delete': 'Borrar', 'files.confirmDelete': '¿Borrar {0}?', 'files.deleteFail': 'No se pudo borrar: {0}',
    'files.uploading': 'Subiendo {0}…', 'files.uploadFail': 'Error en {0}: {1}', 'files.saved': '{0} archivo(s) guardado(s) en {1}/.',
    'files.download': 'Descargar',
    'confirm.reboot': '¿Reiniciar el emulador?',
    'recv.start': 'Recibir del PC', 'recv.stop': 'Detener',
    'recv.how': 'En la computadora, abra la dirección de abajo (o <b>/send</b> en este sitio) y escriba el código. Mantenga esta pantalla abierta.',
    'recv.connecting': 'Conectando…', 'recv.waiting': 'Esperando el PC…', 'recv.pc': 'PC conectado — ya puede enviar.',
    'recv.getting': 'Recibiendo {0}… {1}%', 'recv.saved': '{0} guardado en {1}/.', 'recv.closed': 'Conexión cerrada.',
    'recv.badFile': 'archivo incompleto',
    'send.intro': 'Enviar archivos de este PC a pico-mac en el celular. En el celular, abra 📁 → <b>Recibir del PC</b> y escriba aquí el código que aparece.',
    'send.code': 'Código', 'send.connect': 'Conectar', 'send.folder': 'Carpeta en el celular', 'send.auto': 'automática (por extensión)',
    'send.drop': 'Arrastre los archivos aquí', 'send.pick': 'Elegir archivos…',
    'send.badCode': 'Código inválido (5 letras/números).', 'send.connecting': 'Conectando…', 'send.connected': 'Celular conectado',
    'send.noPhone': 'Celular no encontrado — revise el código y que la pantalla Recibir esté abierta.', 'send.closed': 'Conexión cerrada.',
    'send.phoneGone': 'el celular se desconectó', 'send.queued': 'en cola', 'send.sending': 'enviando…',
    'send.tooBig': 'demasiado grande (máx. 512 MB)', 'send.failed': 'falló',
    'files.mouse': 'Toque en la pantalla',
    'files.mouseTouch': 'El puntero va donde toca el dedo; mantenga para hacer clic y arrastrar',
    'files.mousePad': 'Como un trackpad: deslice para mover, toque para hacer clic',
  },
};

let LANG = (() => {
  const ok = (l) => l && I18N[l.toLowerCase().slice(0, 2)] ? l.toLowerCase().slice(0, 2) : null;
  let saved = null;
  try { saved = new URLSearchParams(location.search).get('lang') || localStorage.getItem('picomac-lang'); } catch (e) {}
  if (ok(saved)) return ok(saved);
  for (const l of navigator.languages || [navigator.language]) if (ok(l)) return ok(l);
  return 'en';
})();

// t('files.saved', 3, 'picomac/disks') -> "3 file(s) saved to picomac/disks/."; unknown keys fall
// back to English, then to the key
function t(key, ...args) {
  const s = (I18N[LANG] && I18N[LANG][key]) ?? I18N.en[key] ?? key;
  return s.replace(/\{(\d)\}/g, (_, i) => args[i] ?? '');
}

function applyI18n(root = document) {
  document.documentElement.lang = { pt: 'pt-BR', en: 'en', es: 'es' }[LANG] || LANG;
  root.querySelectorAll('[data-i18n]').forEach((el) => { el.textContent = t(el.dataset.i18n); });
  root.querySelectorAll('[data-i18n-html]').forEach((el) => { el.innerHTML = t(el.dataset.i18nHtml); });
  root.querySelectorAll('[data-i18n-title]').forEach((el) => { el.title = t(el.dataset.i18nTitle); });
  root.querySelectorAll('[data-i18n-aria]').forEach((el) => { el.setAttribute('aria-label', t(el.dataset.i18nAria)); });
}

function setLang(l) {
  if (!I18N[l]) return;
  LANG = l;
  try { localStorage.setItem('picomac-lang', l); } catch (e) {}
  applyI18n();
  document.dispatchEvent(new Event('picomac-lang'));   // app.js re-renders its dynamic labels
}

applyI18n();
