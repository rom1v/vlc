import { sendCommand } from '../../services/command.service.js';

let player;

export function plyrInit() {
    player = plyr.setup({
        showPosterOnEnd: true
    });
    // setVideo('S6IP_6HG2QE','youtube');
    player[0].on('pause', () => {
        sendCommand(0, 'command=pl_pause');
    });

    player[0].on('play', () => {
        sendCommand(0, 'command=pl_play');
    });

    player[0].on('volumechange', (event) => {
        const cmd = `command=volume&val=${event.detail.plyr.getVolume() * 255}`;
        sendCommand(0, cmd);
    });
    return true;
}

export function setVideo(videoSrc, type) {
    player[0].source({
        type: 'video',
        title: '',
        sources: [{
            src: videoSrc,
            type
        }],
        poster: 'assets/vlc-icons/256x256/vlc.png'
    });
}

export function playItem(src, id) {
    setVideo(src, 'video/mp4', id);
    player[0].play();
    sendCommand(0, `command=pl_play&id=${id}`);
}
