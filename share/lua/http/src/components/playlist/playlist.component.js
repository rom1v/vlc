import { bus } from '../../services/bus.service.js';
import { sendCommand } from '../../services/command.service.js';
import { playItem } from '../player/plyr.methods.js';

Vue.component('playlist', {
    template: '#playlist-template',
    methods: {
        addItem(mode, id, title, src) {
            if (mode === 0) {
                this.$parent.playlistItems.push({
                    id,
                    title,
                    src
                });
            } else if (mode === 1) {
                sendCommand(0, `command=in_enqueue&input=${src}`);
                this.clearPlaylist();
                bus.$emit('refreshPlaylist');
            }
        },
        removeItem(id) {
            sendCommand(0, `command=pl_delete&id=${id}`);
            this.$parent.playlistItems.splice({
                id
            });
            sendCommand(1);
        },
        openPlaylist() {
            $('#playlistNav').width('60%');
            $('#mobilePlaylistNavButton').width('0%');
        },
        closePlaylist() {
            $('#playlistNav').width('0%');
            $('#mobilePlaylistNavButton').width('10%');
        },
        populatePlaylist(playlistData) {
            if (!playlistData) {
                return;
            }
            this.$parent.playlistItems = [];
            for (let i = 0; i < playlistData.children[0].children.length; i++) {
                this.addItem(
                    0,
                    playlistData.children[0].children[i].id,
                    playlistData.children[0].children[i].name,
                    playlistData.children[0].children[i].uri
                );
            }
        },
        fetchPlaylist() {
            sendCommand(1)
                .then(data => this.populatePlaylist(data));
        },
        refreshPlaylist() {
            this.fetchPlaylist();
            setInterval(() => {
                this.fetchPlaylist();
            }, 5000);
        },
        clearPlaylist() {
            this.$parent.playlistItems = [];
        },
        play(src, id) {
            playItem(src,id);
        }
    },
    created() {
        bus.$on('openPlaylist', () => {
            this.openPlaylist();
        });

        bus.$on('closePlaylist', () => {
            this.closePlaylist();
        });

        bus.$on('addItem', (params) => {
            this.addItem(params[0], params[1], params[2], params[3]);
        });

        bus.$on('removeItem', (id) => {
            this.removeItem(id);
        });

        bus.$on('refreshPlaylist', () => {
            this.refreshPlaylist();
        });

        bus.$on('play', (arg) => {
            this.play(arg[0], arg[1]);
        });

        this.refreshPlaylist();
    }
});

$(document).click((e) => {
    const container = $('#playlistNav');

    if ($(window).width() <= 480 && !container.is(e.target) && container.has(e.target).length === 0 && container.css('width') !== '0px') {
        bus.$emit('closePlaylist');
    }
});
