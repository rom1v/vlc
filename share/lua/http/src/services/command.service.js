import { bus } from './bus.service.js';

export function sendCommand(mode, params) {
    if (mode === 0) {
        $.ajax({
            url: 'requests/status.json',
            data: params
        })
        .then((data) => {
            return JSON.parse(data);
        });
    } else if (mode === 1) {
        return $.ajax({
            url: 'requests/playlist.json',
            data: params
        })
        .then((data) => {
            const jsonData = JSON.parse(data);
            bus.$emit('populatePlaylist', jsonData);
            return jsonData;
        });
    } else if (mode === 2) {
        $.ajax({
            url: 'requests/vlm_cmd.xml',
            data: params
        });
    }
}
