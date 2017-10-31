import { bus, notifyBus } from '../../services/bus.service.js';
import { sendCommand } from '../../services/command.service.js';

let playlistItems;

Vue.component('playlist-buttons', {
    template: '#button-template',
    methods: {
        toggleRepeat() {
            sendCommand(0, 'command=pl_repeat');
        },
        startPlaylist() {
            playlistItems = this.$parent.$parent.$data.playlistItems;
            if (playlistItems[0]) {
                notifyBus('play', playlistItems[0].src,playlistItems[0].id);
            }
        },
        toggleRandom() {
            sendCommand(0, 'command=pl_random');
        }
    },
    created() {
        bus.$on('toggleRepeat', () => {
            this.toggleRepeat();
        });

        bus.$on('startPlaylist', () => {
            this.startPlaylist();
        });

        bus.$on('toggleRandom', () => {
            this.toggleRandom();
        });
    }
});
